#include <stdio.h>
#include <cmath>
#include <string>
using std::string;
#include <iostream>
using namespace std;

#include <ixwebsocket/IXNetSystem.h>
#include <ixwebsocket/IXWebSocket.h>
#include <ixwebsocket/IXUserAgent.h>
#include <memory>
#include <GLFW/glfw3.h> 
#include <thread>

#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"
#include "implot.h"
#include "genericWS.h"
#include "calibration/calibration.h"
#include "tools.h"
#include "struct.h"
#include "scheduler.h"
#include "frontend/user_interface.h"


int main(int, char**)
{   // Test sur 20 crypto-monnaies différentes
    std::vector<std::string> symbols = {"BTCUSD", "ETHUSD", "BNBUSD", "SOLUSD", "XRPUSD"};
                                        // "ADAUSD", "BCHUSD", "LTCUSD", "DOTUSD", "TRXUSD", 
                                        // "UNIUSD", "VETUSD", "FILUSD", "ICPUSD", "XLMUSD", 
                                        // "SUIUSD", "TAOUSD", "WLDUSD", "SKYUSD", "APEUSD"

    // Initialisation du manageur télémétrique
    TelemetryManager telemetry_manager(symbols);

    // Initialisation des différentes shared queues pour la communication entre les différentes parties du système
    ThreadSafeQueue<normalized_data> shared_queue;
    ThreadSafeQueue<History> opt_input_queue;
    ThreadSafeQueue<opt_hawkesParams> opt_output_queue;

    // Instanciation des clients websocket pour chaque exchange, et redirection de leur sortie vers la queue partagée
    auto binanceWS  = std::make_unique<BinanceWS>(symbols, 3, shared_queue);
    auto coinbaseWS = std::make_unique<CoinbaseWS>(symbols, 3, shared_queue);
    auto krakenWS   = std::make_unique<KrakenWS>(symbols, 3, shared_queue);
    auto okxWS      = std::make_unique<OkxWS>(symbols, 3, shared_queue);
    auto bybitWS    = std::make_unique<BybitWS>(symbols, 3, shared_queue);

    int calibration_duration = 10; // durée de la phase de calibration en secondes
    int training_duration = 120; // durée de la phase d'entrainement en secondes

    std::vector<std::unique_ptr<GenericWebSocket>> clients;
    // On déplace le pointeur dans le vecteur
    clients.push_back(std::move(binanceWS));
    clients.push_back(std::move(coinbaseWS));
    clients.push_back(std::move(krakenWS));
    clients.push_back(std::move(okxWS));
    clients.push_back(std::move(bybitWS));

    Json::Value websocket_map;
    // On associe à chaque exchange un identifiant unique (entier dans [0, n_websocket-1]) pour faciliter la gestion des données et des modèles
    websocket_map["Binance"] = 0;
    websocket_map["Coinbase"] = 1;
    websocket_map["Kraken"] = 2;
    websocket_map["OKX"] = 3;
    websocket_map["Bybit"] = 4;

    int max_workers = 5; // On définit le nombre maximum de workers HPC qui pourront être utilisés pour l'optimisation, en fonction du nombre de cores disponibles et de la charge de travail estimée. Ce paramètre peut être ajusté en fonction des besoins et des ressources du système.
    Json::Value worker_mapping; // Cette variable contient la répartition des symbols entre les différents workers. Elle est généré dynamiquement par le scheduler pendant la phase de calibration, en fonction du volume de données reçu pour chaque symbol et de la charge de travail associée.

    // Comme pour les websockets, on associe à chaque symbol un identifiant unique (entier dans [0, n_symbols-1]) pour faciliter la gestion des données et des modèles.
    Json::Value symbols_map;
    for (int i = 0; i < symbols.size(); i++) {
        symbols_map[symbols[i]] = i;
    }
    // On regroupe tous les paramètres de configuration du scheduler dans une structure pour faciliter leur passage entre les différentes parties du système.
    SchedulerConfig *schelConfig = new SchedulerConfig();
     
    schelConfig->calibration_time = calibration_duration;
    schelConfig->training_time = training_duration;
    schelConfig->n_max_workers = max_workers;
    schelConfig->symbols = symbols;
    schelConfig->websocket_map = websocket_map;
    schelConfig->worker_mapping = worker_mapping;
    schelConfig->symbols_map = symbols_map;
    
    // Instanciation du moteur de calibration et du scheduler, puis démarrage du système
    CalibrationEngine calibrationE(shared_queue, symbols_map);
    Scheduler scheduler(schelConfig, clients, shared_queue, opt_input_queue, opt_output_queue, calibrationE, telemetry_manager);
    std::cout << "Starting System..." << std::endl;

    UserInterface ui(*schelConfig, telemetry_manager);

    // Démarrage du scheduler dans un thread séparé pour permettre à l'interface utilisateur de fonctionner en parallèle
    std::thread scheduler_thread([&scheduler](){
        scheduler.run();
    });

    ui.main_renderer();

    scheduler_thread.join();

    return 0;
}