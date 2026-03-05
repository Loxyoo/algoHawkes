import multiprocessing
import asyncio
import time
import pandas as pd
import queue  # Pour l'exception Empty
from src.hawkes_model import HawkesWorker
from src.calibration import CalibrationEngine

# --- CONSTANTES ---
IDLE = 0
CALIBRATION = 1
TRAINING = 2
STOPPING = 3

# --- Helper ---
def inverse_key_value(mapping):
    inv_map = {}
    for k, v in mapping.items():
        inv_map.setdefault(v, []).append(k)
    return inv_map

# --- NOUVEAU WORKER DÉDIÉ À L'OPTIMISATION ---
class HawkesOptimizationWorker(multiprocessing.Process):
    """
    Worker dédié au calcul intensif (Ctypes / Nelder-Mead).
    Il tourne en boucle, attend des dataframes, et renvoie les paramètres.
    """
    def __init__(self, input_queue, output_queue, calibration_instance, master_output_queue):
        super().__init__()
        self.input_queue = input_queue
        self.output_queue = output_queue
        # On passe l'instance (ou juste la méthode si picklable) pour accéder à la lib C
        self.calibration_instance = calibration_instance
        self.daemon = True # S'arrête si le main s'arrête brutalement
        self.master_output_queue = master_output_queue

    def run(self):
        print("Optimization Worker: Démarré et en attente.")
        while True:
            try:
                # 1. Attente bloquante d'un job (avec timeout pour pouvoir s'arrêter)
                task = self.input_queue.get(timeout=2)
                
                if task == "STOP":
                    print("Optimization Worker: Arrêt demandé.")
                    break
                
                # 2. Récupération des données
                # task est attendu sous la forme (events_list)
                events_data, symb = task
                if not events_data:
                    continue

                # 3. Calcul Intensif (BLOQUANT ici, mais c'est pas grave, on est dans un autre process)
                # Note: On suppose que optimize_hawkes_c est accessible ici. 
                # Si elle est 'async' dans la classe d'origine, il faut appeler la version synchrone ici
                # car nous ne sommes pas dans une boucle asyncio.
                
                events_df = pd.DataFrame(events_data)
                # Appel direct à la logique C (on suppose que cette méthode n'est pas async ou qu'on appelle la version C directe)
                # Si optimize_hawkes_c est async def, il faut utiliser asyncio.run(), mais idéalement le worker fait du synchrone.
                
                # Astuce: On utilise une méthode helper synchrone dans PassiveCalibration
                # ou on appelle directement la lib C ici si possible.
                # Supposons que optimize_hawkes_c_sync existe:
                self.master_output_queue.put({
                    "type" : "optimization_feedback",
                    "data" : {"symbol" : symb}
                })
                mu, alpha, beta = self.calibration_instance.optimize_hawkes_c_sync(events_df)
                
                # 4. Envoi du résultat
                result = {
                    'status': 'OK',
                    'symb' : symb,
                    'symbol' : symb,
                    'mu': mu, 
                    'alpha': alpha, 
                    'beta': beta,
                    'timestamp': time.time()
                }
                self.output_queue.put(result)
                self.master_output_queue.put({
                    "type" : "optimization_feedback",
                    "data" : result
                })
                
            except queue.Empty:
                continue
            except Exception as e:
                print(f"Erreur dans Optimization Worker: {e}")
                self.output_queue.put({'status': 'ERROR', 'error': str(e)})

# --- ORCHESTRATEUR PRINCIPAL ---
class HFTEngine:

    def __init__(self, calibrate_duration : int,
                 main_phase_duration : int,
                 target_symbols : list[str],
                 clients : list,
                 master_output_queue : multiprocessing.Queue,
                 worker_mapping
                 ):
        
        self.calibrate_duration = calibrate_duration
        self.main_phase_duration= main_phase_duration
        self.target_symbols     = target_symbols
        self.clients            = clients
        
        self.active_exchanges = [client.name for client in self.clients]
        self.ws_map = {ws:i for i, ws in enumerate(self.active_exchanges)}
        
        self.status = IDLE
        self.mp_manager = multiprocessing.Manager()
        
        # Queues
        self.calibration_queue = asyncio.Queue()
        self.worker_queues = {} 
        
        # Queues spécifiques pour l'optimiseur
        self.opt_input_queue = self.mp_manager.Queue()
        self.opt_output_queue = self.mp_manager.Queue()
        
        self.mapping = None
        self.workers = [] 
        self.opt_worker = None # Référence au process optimiseur
        
        self.passive_calibration = None 
        self.buffer_events = [] 

        self.master_output_queue = master_output_queue

        if worker_mapping == None:
            self.re_mapping = True
        self.mapping = worker_mapping

    async def router_callback(self, data):
        """Dispatche les données."""
        if data is None: return

        if self.status == CALIBRATION:
            await self.calibration_queue.put(data)
            
        elif self.status == TRAINING:
            # Accumulation pour l'optimiseur (Main Process Memory)
            self.buffer_events.append({
                'time': data.get('arrival_time'),
                'type': self.ws_map.get(data.get('exchange'), 0),
                'symbol': data.get('symbol')
            })
            
            # Dispatch vers les Trading Workers
            try:
                symbol = data.get("symbol")
                if symbol and self.mapping and symbol in self.mapping:
                    worker_id = self.mapping[symbol]
                    self.worker_queues[worker_id].put_nowait(data)
            except Exception:
                pass

    # ... (start_clients et run_calibration identiques) ...
    def start_clients(self):
        return [asyncio.create_task(client.listen(self.router_callback)) for client in self.clients]
    
    async def run_calibration(self):
        # ... (Identique à ton code) ...
        print("\n--- PHASE 1 : CALIBRATION ---")
        self.status = CALIBRATION
        calib_engine = CalibrationEngine(self.calibrate_duration, self.target_symbols, self.calibration_queue, self.ws_map, 2)
        if self.mapping == None:
            self.mapping = await calib_engine.run()
        self.passive_calibration = calib_engine.passiveCalibration
        self.master_output_queue.put({"type" : "calibration_feedback", "data" : self.mapping})
        print(f"tatus : OK | Mapping final : {self.mapping}")

    async def run_training(self):
        print("\n--- PHASE 2 : TRAINING (HPC) ---")
        if not self.mapping or not self.passive_calibration:
            raise RuntimeError("Initialisation incomplète !")

        self.status = TRAINING
        end_time = time.time() + self.main_phase_duration
        
        # 1. Lancement des Workers de Trading (Core Packing)
        worker_assets = inverse_key_value(self.mapping)
        self.worker_queues = {}
        self.workers = []

        for w_id, assets in worker_assets.items():
            mp_queue = self.mp_manager.Queue() # Queue réservé pour les données websocket
            self.worker_queues[w_id] = mp_queue
            # On donne les queues vers le worker d'optimisation, on construit un pont entre ces deux coeurs physique
            p = HawkesWorker(w_id, assets, mp_queue, self.master_output_queue, end_time, self.ws_map, self.opt_input_queue, self.opt_output_queue) 
            self.workers.append(p)
            p.start()

        # 2. Lancement du Worker d'Optimisation
        # Note: self.passive_calibration doit être picklable (pas de lock asyncio interne)
        self.opt_worker = HawkesOptimizationWorker(
            self.opt_input_queue, 
            self.opt_output_queue, 
            self.passive_calibration,
            self.master_output_queue
        )
        self.opt_worker.start()
        
        print(f"{len(self.workers)} Trading Workers + 1 Optimization Worker lancés")

        # 3. Boucle Principale
        try:
            elapsed = 0
            # Intervalle d'optimisation (10s)
            OPTIM_INTERVAL = 10
            last_optim_time = 0
            
            start_loop = time.time()
            
            while elapsed < self.main_phase_duration:
                now = time.time()
                elapsed = now - start_loop
                # Petite pause pour laisser respirer l'event loop asyncio
                # C'est ici que les websockets sont traités via router_callback
                await asyncio.sleep(0.1) 
                    
        except asyncio.CancelledError:
            print("Interruption Training...")

        # 4. Arrêt propre
        self.status = STOPPING
        print(" Arrêt des workers...")
        
        # Stop Trading Workers
        for q in self.worker_queues.values(): q.put("STOP")
        # Stop Optimizer
        self.opt_input_queue.put("STOP")
            
        for p in self.workers:
            p.join(timeout=2)
            if p.is_alive(): p.terminate()
            
        if self.opt_worker:
            self.opt_worker.join(timeout=2)
            if self.opt_worker.is_alive(): self.opt_worker.terminate()

    async def stop_clients(self, ws_tasks):
        # ... (Identique) ...
        print("Fermeture connexions...")
        for client in self.clients:
            await client.close()
        for task in ws_tasks:
            task.cancel()

    async def run(self):
        ws_tasks = self.start_clients()
        await self.run_calibration()
        await self.run_training()
        await self.stop_clients(ws_tasks)
        print("Status : OK | Fin du programme.")