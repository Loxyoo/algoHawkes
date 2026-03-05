"""
  ░██████             ░██ ░██░██                               ░██    ░██                      
 ░██   ░██            ░██    ░██                               ░██                             
░██         ░██████   ░██ ░██░████████  ░██░████  ░██████   ░████████ ░██ ░███████  ░████████  
░██              ░██  ░██ ░██░██    ░██ ░███           ░██     ░██    ░██░██    ░██ ░██    ░██ 
░██         ░███████  ░██ ░██░██    ░██ ░██       ░███████     ░██    ░██░██    ░██ ░██    ░██ 
 ░██   ░██ ░██   ░██  ░██ ░██░███   ░██ ░██      ░██   ░██     ░██    ░██░██    ░██ ░██    ░██ 
  ░██████   ░█████░██ ░██ ░██░██░█████  ░██       ░█████░██     ░████ ░██ ░███████  ░██    ░██                                                                                   
"""
from dataclasses import dataclass
# from GenericWebsocket import GenericWebSocket # Supposé importé
import socket, json, time, asyncio, os, heapq, requests, re, platform
import logging
import multiprocessing # Indispensable pour les Queues partagées
import queue # Pour capturer l'exception Queue.Empty
import numpy as np
from abc import ABC, abstractmethod
import ctypes
import pandas as pd
# from GenericWebsocket import *

class CalibrationEngine:
    """
    Gère la mesure des volumes et la répartition de charge (Load Balancing).
    Tourne dans le processus principal (Asyncio).
    """

    def __init__(self, 
                 duration_calibrage : int, 
                 cryptos : list[str],
                 inqueue : asyncio.Queue,
                 ws_map : dict[str, int],
                 n_max_workers : int = None):
        
        self.inqueue            = inqueue
        self.duration_calibrage = duration_calibrage
        self.cryptos            = cryptos

        self.ws_map = ws_map
        
        self.buffer_times = []
        self.buffer_src   = []

        # Logique Cœurs
        self.n_logical_cores    = os.cpu_count() or 4
        N_SECURITY_MARGIN = 2
        available_cores = max(1, self.n_logical_cores - N_SECURITY_MARGIN)

        if n_max_workers is None:
            self.n_max_workers = available_cores
        else:
            self.n_max_workers = min(n_max_workers, available_cores)

        self.volumes = {crypto: 0 for crypto in cryptos}
        self.mapping = {}

        T_max = time.time() + duration_calibrage*1000
        self.passiveCalibration = PassiveCalibration(len(self.ws_map.keys()), T_max)

    def cores_packing(self, assets_volume : dict[str, int]) -> dict[int, list[str]]:
        """Algorithme Greedy Bin-Packing."""
        sorted_assets = sorted(assets_volume.items(), key=lambda x: x[1], reverse=True)
        workers_heap = [(0, i, []) for i in range(self.n_max_workers)]
        heapq.heapify(workers_heap)
        
        mapping_table = {}
        
        for asset, volume in sorted_assets:
            current_load, worker_id, assigned_list = heapq.heappop(workers_heap)
            assigned_list.append(asset)
            mapping_table[asset] = worker_id
            new_load = current_load + volume
            heapq.heappush(workers_heap, (new_load, worker_id, assigned_list))
            
        return mapping_table, workers_heap
    
    async def process_data(self):
        """Consomme la donnée de la queue Asyncio."""
        data = await self.inqueue.get()
        symbol = data.get('symbol')
        if symbol in self.volumes:
            self.volumes[symbol] += 1
        self.inqueue.task_done()

    def get_buffers(self):
        return self.buffer_times, self.buffer_times


    async def run(self):
        print(f"⚖️  Début Calibration ({self.duration_calibrage}s)...")
        end_time = time.time() + self.duration_calibrage
        
        while True:
            remaining = end_time - time.time()
            if remaining <= 0: break
            try:
                await asyncio.wait_for(self.process_data(), timeout=remaining)
            except asyncio.TimeoutError:
                break
            except Exception as e:
                print(f"Err Calib: {e}")

        mapping, _ = self.cores_packing(self.volumes)
        self.mapping = mapping
        return mapping


class PassiveCalibration(multiprocessing.Process):
    """
    Cette classe sera envoyé à un coeur physique du processeur dédié pour le calcul intensif. C'est ici que le code C
    s'excutera.
    """
    def __init__(self, n_ws, t_max):
        super().__init__()
        # 1. Charger la librairie C
        # Sous Windows, remplacez par .dll
        self.lib_path = os.path.join(os.path.dirname(__file__), "calculations/lib/libhawkes.so")
        self.lib = ctypes.CDLL(self.lib_path) 

        # 2. Définir la signature de la fonction C pour que Python sache quoi envoyer
        # void python_entry_point(double* times, int* types, int count, double T_max, int n_ws, double* out_mu, ...)
        self.lib.python_entry_point.argtypes = [
            ctypes.POINTER(ctypes.c_double), # times
            ctypes.POINTER(ctypes.c_int),    # types
            ctypes.c_int,                    # count
            ctypes.c_double,                 # T_max
            ctypes.c_int,                    # n_ws
            ctypes.POINTER(ctypes.c_double), # out_mu
            ctypes.POINTER(ctypes.c_double), # out_alpha
            ctypes.POINTER(ctypes.c_double)  # out_beta
        ]
        self.lib.python_entry_point.restype = None # La fonction renvoie void

        self.n_ws = n_ws
        self.t_max = t_max

        # Stocke le chemin absolu pour être sûr de le retrouver
        # self.lib_path = os.path.abspath("./lib/libhawkes.so") 
        
        # Charge la lib (on met ça dans une méthode à part pour pouvoir la réutiliser)
        self._load_library()

    def _load_library(self):
        """Méthode helper pour charger la DLL et configurer les types"""
        try:
            self.lib = ctypes.CDLL(self.lib_path)
            
            # --- Configuration des signatures (argtypes) ---
            # IMPORTANT : Copie ici toutes les définitions argtypes que tu avais dans __init__
            self.lib.python_entry_point.argtypes = [
                ctypes.POINTER(ctypes.c_double), # times
                ctypes.POINTER(ctypes.c_int),    # types
                ctypes.c_int,                    # count
                ctypes.c_double,                 # T_max
                ctypes.c_int,                    # n_ws
                ctypes.POINTER(ctypes.c_double), # out_mu
                ctypes.POINTER(ctypes.c_double), # out_alpha
                ctypes.POINTER(ctypes.c_double)  # out_beta
            ]
            self.lib.python_entry_point.restype = None
            print(f"Librairie C chargée depuis : {self.lib_path}")
            
        except OSError as e:
            print(f"⚠️ Erreur chargement lib C: {e}")
            self.lib = None

    # --- C'est ici que la magie du Multiprocessing opère ---

    def __getstate__(self):
        """Appelé lors du pickling (avant l'envoi au worker)"""
        state = self.__dict__.copy()
        # On supprime l'objet ctypes (non sérialisable) de l'état à envoyer
        if 'lib' in state:
            del state['lib']
        return state

    def __setstate__(self, state):
        """Appelé lors de l'unpickling (à l'arrivée dans le worker)"""
        # On restaure les attributs classiques (mu, alpha, mapping, etc.)
        self.__dict__.update(state)
        # On RECHARGE la librairie C dans ce nouveau processus
        self._load_library()

    def optimize_hawkes_c_sync(self, events_df : pd.DataFrame):
        """
        Fonction Python qui prépare les données et appelle le C. Fonction synchrone utilisé dans le worker calcul intensif
        """
        # Conversion DataFrame ou Liste -> Numpy Array contigus (Important !)
        times = np.ascontiguousarray(events_df['time'].values, dtype=np.float64)
        types = np.ascontiguousarray(events_df['type'].values, dtype=np.int32)
        count = len(times)

        # Préparation des buffers de sortie (tableaux vides que le C va remplir)
        # C'est comme donner une feuille blanche au code C
        out_mu = np.zeros(self.n_ws, dtype=np.float64)
        out_alpha = np.zeros(self.n_ws * self.n_ws, dtype=np.float64)
        out_beta = np.zeros(self.n_ws * self.n_ws, dtype=np.float64)

        print(f"Envoi de {count} événements au moteur C...")
        start_time = time.time()

        # APPEL MAGIQUE AU C
        self.lib.python_entry_point(
            times.ctypes.data_as(ctypes.POINTER(ctypes.c_double)),
            types.ctypes.data_as(ctypes.POINTER(ctypes.c_int)),
            count,
            self.t_max,
            self.n_ws,
            out_mu.ctypes.data_as(ctypes.POINTER(ctypes.c_double)),
            out_alpha.ctypes.data_as(ctypes.POINTER(ctypes.c_double)),
            out_beta.ctypes.data_as(ctypes.POINTER(ctypes.c_double))
        )

        print(f"Optimisation C terminée en {time.time() - start_time:.4f}s")

        # Reshape des résultats à plat vers des matrices
        res_mu = out_mu
        res_alpha = out_alpha.reshape((self.n_ws, self.n_ws))
        res_beta = out_beta.reshape((self.n_ws, self.n_ws))

        return res_mu, res_alpha, res_beta

    async def optimize_hawkes_c(self, events_df : pd.DataFrame):
        """
        Fonction Python qui prépare les données et appelle le C.
        """
        # Conversion DataFrame ou Liste -> Numpy Array contigus (Important !)
        times = np.ascontiguousarray(events_df['time'].values, dtype=np.float64)
        types = np.ascontiguousarray(events_df['type'].values, dtype=np.int32)
        count = len(times)

        # Préparation des buffers de sortie (tableaux vides que le C va remplir)
        # C'est comme donner une feuille blanche au code C
        out_mu = np.zeros(self.n_ws, dtype=np.float64)
        out_alpha = np.zeros(self.n_ws * self.n_ws, dtype=np.float64)
        out_beta = np.zeros(self.n_ws * self.n_ws, dtype=np.float64)

        print(f"Envoi de {count} événements au moteur C...")
        start_time = time.time()

        # APPEL MAGIQUE AU C
        self.lib.python_entry_point(
            times.ctypes.data_as(ctypes.POINTER(ctypes.c_double)),
            types.ctypes.data_as(ctypes.POINTER(ctypes.c_int)),
            count,
            self.t_max,
            self.n_ws,
            out_mu.ctypes.data_as(ctypes.POINTER(ctypes.c_double)),
            out_alpha.ctypes.data_as(ctypes.POINTER(ctypes.c_double)),
            out_beta.ctypes.data_as(ctypes.POINTER(ctypes.c_double))
        )

        print(f"Optimisation C terminée en {time.time() - start_time:.4f}s")

        # Reshape des résultats à plat vers des matrices
        res_mu = out_mu
        res_alpha = out_alpha.reshape((self.n_ws, self.n_ws))
        res_beta = out_beta.reshape((self.n_ws, self.n_ws))

        return res_mu, res_alpha, res_beta
    
    # Méthodes pour une calibration passive
    # Sélection des actifs selon une régression LASSO
    # Analyse des performances et de la qualité du modèle grâce à des tests
    # Analyse des résidus du modèle, vérifie que l'EMV est bien
    def update_lasso_selection(self, data):
        """ 
        Cette méthode utilise la régression de LASSO. Permet de réaliser une sélection des actifs de facon passif
        en appliquant des pénalités. Cette technique permet de réduire les calculs et de garder uniquement les
        actifs les plus intéressantes.
        """
        pass

    def passive_calibration(self, data): 
        """
        Calibration passive, analyse les performances et la qualités du modèles. Elle vérifie le bon ajustement grâce
        à des intervalles de confiances, des tests et des analyses de résidus grâce à des méthodes complexe et adaptés
        pour les processus de Hawkes multivarié. 
        Cette méthode, ne réalise pas ses analyse en continue, mais se fera en fonction d'un intervalle de temps
        prédéfini (ex: 60s).

        Exemple de tests :
        - Calcul du lead-lag avec la méthode naïve grâce à une grille de latence, et création d'intervalle de confiance stable
        - Intervalle de confiance de risque 10%
        - Analyse des résidus du modèle
        - Tests d'hypothèses sur les paramètres estimés
        - Redémarrage du modèle en cas de dégénérescence
        - Recalibration des workers
        """
        pass

class GuardRails:
    def __init__(self):
        pass

    # Les Guardes-fous analysent en continu les performances et les décisions de l'algorithmes
    # Si quelque chose ne va pas et qui semble anormal, les gardes-fous peuvent stopper les connexions et
    # arrêté toutes activités. C'est une sécurité qui permet d'éviter de grosses perte.
    def guardrails(self):
        """Barrière de sécurité contre les pertes et les décisions/résultats anormaux."""
        pass

class KillSwitch:
    def __init__(self):
        pass
    
    def kill_switch(self):
        """Arrêt du Worker ou de l'algo"""
        pass

