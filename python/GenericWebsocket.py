import asyncio
import json
import websockets
import csv
from abc import ABC, abstractmethod
from datetime import datetime
from stringcolor import * 
from src.toolsWS import reformat_tickers

# --- CLASSE MÈRE (GÉNÉRIQUE) ---
class GenericWebSocket(ABC):
    """
    Classe de base abstraite pour tout client WebSocket.
    Gère la connexion, la reconnexion et la boucle d'événements.
    """
    def __init__(self, name, symbols : list[str], market_datas_output_queue):

        self.config = {}
        with open("src/config.json", "r") as f:
            self.config = json.load(f)

        self.name = name

        # Symbols parsing
        self.uri = self.config[self.name]["url"]
        self.fmt = self.config[self.name]["request_format"]["currency_format"]
        is_lower = self.config[self.name]["request_format"].get("is_lowercase", False)
        self.formated_symbols = reformat_tickers(symbols, self.fmt, is_lower)

        self.websocket = None
        self.is_running = False
        self.N = 0 # Compteur de données
        self.filename = "market_datas"

        self.market_datas_output_queue = market_datas_output_queue
        
    async def connect(self):
        try:
            self.websocket = await websockets.connect(self.uri)
            print(f"Status : OK | [{self.name}] Connecté à {self.uri}")
            await self.subscribe() # S'abonne automatiquement après connexion
        except Exception as e:
            
            print(f"[{self.name}] Erreur de connexion : {e}")

    async def listen(self, callback):
        self.is_running = True
        # On retire l'ouverture du fichier ici !
        
        while self.is_running:
            try:
                if not self.websocket:
                    await self.connect()
                
                async for message in self.websocket:
                    data = json.loads(message)
                    
                    # Normalisation
                    normalized_data = self.normalize_message(data)

                    if normalized_data:
                        # Gestion du cas où un échange renvoie une LISTE de trades (ex: OKX)
                        if isinstance(normalized_data, list):
                            for item in normalized_data:
                                self.market_datas_output_queue.put(item)
                                await callback(item)
                                self.N += 1
                        else:
                            self.market_datas_output_queue.put(normalized_data)
                            await callback(normalized_data)
                            self.N += 1
                        
            except websockets.ConnectionClosed:
                print(f" [{self.name}] Connexion perdue. Reconnexion...")
                await asyncio.sleep(2)
                self.websocket = None 
            except Exception as e:
                print(f"[{self.name}] Erreur critique : {e}")
                await asyncio.sleep(1)

    async def close(self):
        """Ferme proprement la connexion"""
        self.is_running = False
        if self.websocket:
            await self.websocket.close()
            print(f"[{self.name}] Socket fermé.")

    async def subscribe(self):
        payload = self.config[self.name]["request_format"]["subscription_format"]
        params_key = self.config[self.name]["request_format"]["params_key"]
        payload[params_key] = self.formated_symbols
        await self.websocket.send(json.dumps(payload))
        print(f"[{self.name}] Abonné à : {self.formated_symbols}")

    async def unsubscribe(self, standard_formated_symbols=None):
        """
        Se désabonne des flux de données spécifié par standard_formated_symbols. Si None, se désabonne de tous les symboles abonnés.
        
        Parameters:
            standard_formated_symbols (list[str]): Liste des symboles au format standard (ex: BTCUSD, XRPUSD, ETHUSD).
        """
        if standard_formated_symbols:
            params = reformat_tickers(standard_formated_symbols, self.fmt)
        else:
            params = self.formated_symbols
        payload = self.config[self.name]["request_format"]["unsubscription_format"]
        params_key = self.config[self.name]["request_format"]["params_key"]
        payload[params_key] = params
        if self.websocket:
            await self.websocket.send(json.dumps(payload))
            print(f" [{self.name}] Unsubscribed:",self.formated_symbols)
        else:
            print(f" [{self.name}] Impossible de se désabonner, pas de connexion active.")

    @abstractmethod
    def normalize_message(self, data):
        """Méthode abstraite : transformer le JSON brut en format standard (Prix, Qte, Timestamp)."""
        pass

# --- Public API Websocket ---
class BinanceWS(GenericWebSocket):
    """API Websocket de Binance (basé en Asie)"""
    def __init__(self, symbols, market_datas_output_queue):
        # URL Binance Futures ou Spot selon vos besoins
        super().__init__(name="Binance", symbols=symbols, market_datas_output_queue=market_datas_output_queue)
        self.stream_type = "aggTrade"  # Type de flux pour les trades agrégés

    def normalize_message(self, data):
        # Filtre pour ne garder que les messages du stream voulu
        if "e" in data and data["e"] == self.stream_type:
            return {
                "exchange": self.name,
                "symbol": data['s'][:-1], # Enlève le "T" à la fin
                "price": float(data['p']),
                "quantity": float(data['q']),
                "timestamp": data['T'], # Timestamp événement (Event Time)
                "arrival_time": datetime.now().timestamp() # Timestamp local
            }
        return None
    

class CoinbaseWS(GenericWebSocket):
    """API Websocket de Coinbase (basé au USA)"""

    def __init__(self, symbols, market_datas_output_queue):
        super().__init__(name="Coinbase", symbols=symbols, market_datas_output_queue=market_datas_output_queue)

    def normalize_message(self, data):
        if data.get("channel") == "ticker":
            # ... (votre code de récupération des tickers)
            ticker_info = data['events'][0]['tickers'][0]
            
            # --- CORRECTION DU TIMESTAMP ---
            ts_str = data['timestamp'].replace('Z', '+00:00') # Standardisation UTC
            
            # Si on détecte trop de précision (nanosecondes), on coupe
            # Format attendu : YYYY-MM-DDTHH:MM:SS.mmmmmm (26 caractères)
            # On coupe la partie fractionnaire à 6 chiffres maximum
            if "." in ts_str:
                head, sep, tail = ts_str.partition(".")
                # tail contient "011057305+00:00"
                # On sépare les chiffres du timezone (+00:00)
                if "+" in tail:
                    digits, tz = tail.split("+")
                    digits = digits[:6] # On garde max 6 chiffres
                    ts_str = f"{head}.{digits}+{tz}"
                else:
                    # Cas rare sans timezone explicite
                    ts_str = f"{head}.{tail[:6]}"

            # Conversion
            dt_obj = datetime.fromisoformat(ts_str)
            ts_ms = dt_obj.timestamp() * 1000
            # -------------------------------

            return {
                "exchange": self.name,
                "symbol": ticker_info['product_id'].replace("-", ""),
                "price": float(ticker_info['price']),
                "quantity": 0.0, 
                "timestamp": ts_ms,
                "arrival_time": datetime.now().timestamp()
            }
        return None

class KrakenWS(GenericWebSocket):
    """API Websocket de Kraken (données Spot)"""

    def __init__(self, symbols: list[str], market_datas_output_queue):
        super().__init__(name="Kraken", symbols=symbols, market_datas_output_queue=market_datas_output_queue) 

    def normalize_message(self, data):
        # Vérification stricte que c'est une liste de données
        if isinstance(data, list) and len(data) >= 2:
            # Kraken met parfois le channelName en avant-dernier ou dernier selon la config
            # Le format standard ticker est : [channelID, {data}, channelName, pair]
            
            # On cherche le dict qui contient 'c' (close)
            ticker_data = None
            pair = None
            
            for item in data:
                if isinstance(item, dict) and 'c' in item:
                    ticker_data = item
                if isinstance(item, str) and '/' in item:
                    pair = item
            
            if ticker_data and pair:
                close_data = ticker_data.get('c', [])
                if not close_data: return None

                symbol = pair.replace('/', '')
                if "XBT" in symbol: symbol = symbol.replace("XBT", "BTC")

                return {
                    "exchange": self.name,
                    "symbol": symbol,
                    "price": float(close_data[0]),
                    "quantity": float(close_data[1]),
                    "timestamp": int(datetime.now().timestamp() * 1000), 
                    "arrival_time": datetime.now().timestamp()
                }
        return None

class OkxWS(GenericWebSocket):
    """API Websocket de OKX (basé à Hong Kong)"""
    
    def __init__(self, symbols : list[str], market_datas_output_queue):
        # URI correcte pour le public
        super().__init__(name="OKX", symbols=symbols, market_datas_output_queue=market_datas_output_queue) 
        self.args_payload = []

    def get_args_payload(self, params : list[str]):
        args = []
        for symbol in params:
            args.append({
                "channel": "trades", # Canal 'trades' pour les exécutions HFT
                "instId": symbol      # L'instrument (ex: BTC-USDT)
            })
        return args
    
    # Override subscribe method
    async def subscribe(self):
        # Cas particulier pour OKX
        # OKX attend une liste d'objets (dictionnaires) dans 'args'
        payload = {
            "op": "subscribe",
            "args": self.get_args_payload(self.formated_symbols)
        }
        await self.websocket.send(json.dumps(payload))
        print(f"[{self.name}] Abonné au canal 'trades' pour : {self.formated_symbols}")
    
    # Override unsubscribe method
    async def unsubscribe(self, params=None):
        if params is None:
            params = self.formated_symbols
        else :
            params = reformat_tickers(params, self.fmt)
        args_payload = self.get_args_payload(params)
        payload = {
            "op": "unsubscribe",
            "args": args_payload # Utilise la liste d'objets construite
        }
        if self.websocket:
            await self.websocket.send(json.dumps(payload))
            print(f" [{self.name}] Unsubscribed:", params)
    
    def normalize_message(self, data):
        if data.get('event') in ['subscribe', 'unsubscribe']:
             return None 

        if data.get('arg', {}).get('channel') == 'trades' and 'data' in data:
            results = [] # On prépare une liste
            for trade in data['data']:
                symbol = trade['instId'].replace('-', '')[:-1] # BTC-USDT -> BTCUSDT
                
                results.append({
                    "exchange": self.name,
                    "symbol": symbol,
                    "price": float(trade['px']),
                    "quantity": float(trade['sz']),
                    "timestamp": int(trade['ts']),
                    "arrival_time": datetime.now().timestamp()
                })
            return results # On retourne TOUS les trades du paquet
        return None
    
class BybitWS(GenericWebSocket):
    """API Websocket de Bybit (basé à Singapour)"""

    def __init__(self, symbols : list[str], market_datas_output_queue):
        super().__init__(name="Bybit", symbols=symbols, market_datas_output_queue=market_datas_output_queue)
        self.symbols = symbols
    
    def normalize_message(self, data):

        if data.get('op') == 'subscribe':
            return None
        
        if data.get('topic') in self.formated_symbols:
            trade = data.get('data')
            return {
                "exchange" : self.name,
                "symbol": trade['symbol'][:-1], # Enlève le 'T' à la fin
                "price": float(trade['lastPrice']),
                "quantity": float(trade['volume24h']),
                "timestamp": int(data['ts']), # Timestamp en millisecondes, pas besoin de conversion
                "arrival_time": datetime.now().timestamp()
            }

