import asyncio
import numpy as np
import multiprocessing
import time
import queue

# --- WORKER PROCESS (CŒUR DE CALCUL) ---
class HawkesWorker(multiprocessing.Process):
    """
    Processus indépendant qui héberge les modèles de Hawkes.
    Gère les dimensions par paire unique (Symbole, Échange).
    """

    def __init__(self, 
                 worker_id: int,                                # Identifiant du worker
                 assets : list[str],                            # Assets gérés par ce worker
                 input_queue: multiprocessing.Queue,            # Queue pour les données websocket
                 master_output_queue : multiprocessing.Queue,   # Queue master pour permettre l'analyse de données
                 end_time: float,                               # Fin de l'entrainement (ne run pas encore indéfiniment)
                 ws_map : dict,                                 # Mappage entre le nom du websocket et un identifiant integer
                 opt_input_queue : multiprocessing.Queue,       # Queue pour l'envoie de données vers le worker d'opimisation
                 opt_output_queue : multiprocessing.Queue       # Queue pour la réception des paramètres optimisé par le worker d'optimisation
                 ):
        """
        :param associated_keys: Liste de tuples ex: [('BTCUSD', 'binance'), ('BTCUSD', 'coinbase'), ...]
        """
        super().__init__()
        self.worker_id = worker_id          # Identifiant du worker
        self.input_queue = input_queue      # Stream de données vers ce worker
        self.assets = assets                # Les assets géré par ce worker
        self.n_ws = len(ws_map.keys())   # Nombre de websocket actif
        
        self.ws_map = ws_map
        
        self.end_time = end_time
        self.batch_size = 4                 # Taille des batch pour l'EMV accéléré
        
        # Dimensions : Nom
        self.n_model = len(assets)          # Nombre de modèle géré par ce worker

        # Queue pour communiqué avec le worker d'optimisation (réservé pour le calcul intensif)
        self.opt_input_queue = opt_input_queue
        self.opt_output_queue = opt_output_queue

        # Création d'un modèle de Hawkes pour chaque actif géré par ce worker.
        self.models = {symbol:HawkesModel(self.worker_id, self.n_ws, symbol, master_output_queue, 
                                          self.input_queue, self.end_time, self.ws_map) for symbol in self.assets}

    def _init_models(self):
        """Initialisation des matrices numpy locale au processus"""
        for model in self.models.values():
            model._init_model_params()

    def update_worker(self, data):
        """
        Mise à jour du worker
        """
        pass
        
    def run(self):
        print(f"Worker {self.worker_id} démarré. Gestion de : {self.assets}")
        self._init_models()
        OPTIM_INTERVAL = 10
        last_optim_time = 0
        
        while True:
            now = time.time()
            if now > self.end_time:
                break
            try:
                data = self.input_queue.get(timeout=0.5)
                if data == "STOP": break
                
                # Mise à jour du modèle de Hawkes associé au symbol de data
                self.models.get(data.get("symbol")).update_model(data)
                # Toutes les 10 secondes environ, on optimise les paramètres
                # -> Communiquer avec le worker d'optimisation avec les nouvelles données recu
                if now - last_optim_time >= OPTIM_INTERVAL:
                    for symb, model in self.models.items():
                        if model.n_data > 10:
                            print(f"Envoi de {model.n_data} events au worker d'optimisation")
                            self.opt_input_queue.put([list(model.buffer_events), symb])
                            last_optim_time = now
                            # Permet de dire, 'maintenant tu peux envoyer les données pour l'analyse'
                            # self.models[symb].parameters_optimized = True
                        else:
                            print("Not enough data to optimize.")
                            last_optim_time = now
                # Récupération des paramètres optimisé
                try: 
                    res = self.opt_output_queue.get_nowait()
                    if res['status'] == 'OK':
                        print(f"Nouveaux paramètres reçus (lag : {time.time()- res['timestamp']:.2f}s)")
                        mu, alpha, beta = res['mu'], res['alpha'], res['beta']
                        symb = res['symb']
                        
                        if symb in self.assets:
                            
                            # Diffusion de ces nouveaux paramètres
                            # mu est 1D (n_ws,), on le reshape en 2D (n_ws, n_ws) si nécessaire
                            self.models[symb].alpha = alpha
                            self.models[symb].beta  = beta
                            self.models[symb].v     = mu
                            # print(mu, alpha, beta)

                    elif res['status'] == 'ERROR':
                        print(f"Erreur worker optimisation: {res['error']}")
                except queue.Empty:
                    pass

                # await asyncio.sleep(0.1)
            except asyncio.CancelledError:
                print("Interruption du Training !")
                
            except queue.Empty:
                continue
            except Exception as e:
                print(f"Erreur Worker {self.worker_id}: {e}")
                import traceback
                traceback.print_exc()
                break


class HawkesModel:

    def __init__(self, 
                 worker_id : int, 
                 n_ws : int, 
                 asset : str, 
                 master_output_queue : multiprocessing.Queue,
                 input_queue: multiprocessing.Queue, 
                 end_time: float, 
                 ws_map: dict[str, int]):
        self.worker_id = worker_id      # Identifiant du worker
        self.n_ws = n_ws                # Nombre de websocket actif
        self.asset = asset              # Nom de l'asset
        self.input_queue = input_queue 
        self.T = end_time

        self.intensities = None         # Intensités de Hawkes
        self.ws_map = ws_map
        self.inversed_ws_map = {value:key for key, value in self.ws_map.items()}
        # Paramètres en placeholder
        self.alpha  = None # Force
        self.beta   = None # Oubli
        self.last_t = None # Temp d'observation précédent
        self.i_base = None # Intensité de fond
        self.v      = None # Coeff. de branchement

        # Buffer des temps de l'actif pour chaque websockets actif
        self.buffer_events = [] 
        self.n_data = 0

        # Analyse des données / Dashboard
        self.master_output_queue = master_output_queue

        self.parameters_optimized = False

    def _init_model_params(self):
        """Initialisation des matrices numpy locale au processus"""
        self.alpha  = np.zeros((self.n_ws, self.n_ws), dtype=float)
        self.beta   = np.zeros((self.n_ws, self.n_ws), dtype=float)
        
        # self.v correspond à mu (intensité de fond)
        self.v      = np.zeros(self.n_ws, dtype=float)
        
        # Temps du dernier événement GLOBAL (et non par flux, c'est plus simple)
        # Mais pour être précis en asynchrone multivarié, gardons un vecteur last_t par dimension
        self.last_t = np.zeros(self.n_ws, dtype=float) 
        
        # --- NOUVEAU ---
        # Matrice Phi pour la mise à jour récursive de l'intensité
        # phi[j, k] stocke la somme pondérée des exp(-beta * (t - t_k))
        self.phi = np.zeros((self.n_ws, self.n_ws), dtype=float)
        
        self.intensities = np.zeros(self.n_ws, dtype=float)

    def hawkes_kernel(self, alpha, beta, dt):
        return alpha * np.exp(-beta * dt)

    def update_model(self, data):
        """
        Met à jour l'intensité du modèle de Hawkes de manière récursive.
        """
        # Récupération des infos de l'événement actuel
        source_idx = self.ws_map.get(data.get("exchange"))
        if source_idx is None: return
        
        current_time = data.get("arrival_time", time.time())
        
        prev_global_time = np.max(self.last_t) if np.max(self.last_t) > 0 else current_time
        
        dt = current_time - prev_global_time
        
        # Update de l'excitation phi
        if dt > 0:
            # Hadamard product
            self.phi *= np.exp(-self.beta * dt)
            
        # Calcul de l'intensité instantannée
        for target_idx in range(self.n_ws):
            excitation = np.sum(self.alpha[target_idx, :] * self.phi[target_idx, :])
            self.intensities[target_idx] = self.v[target_idx] + excitation
            self.master_output_queue.put({"type" : "intensity", "data" : {"value" : self.intensities[target_idx], 
                                                                          "src" : self.inversed_ws_map[target_idx],
                                                                          "time" : current_time,
                                                                          "symbol" : self.asset}})
        
        # Update de phi selon la formule en ajoutant le +1
        self.phi[:, source_idx] += 1.0
        
        # Mise à jour du temps
        self.last_t[source_idx] = current_time
        
        # print(self.intensities)
        self.buffer_events.append({
            'time': current_time,
            'type': source_idx,
            'symbol': data.get('symbol')
        })
        data['symbol'] = self.asset
        self.master_output_queue.put({"type" : "market_data", "data" : data})
        self.n_data += 1

