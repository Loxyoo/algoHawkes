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
    std::vector<std::string> symbols = {"BTCUSD", "ETHUSD", "BNBUSD", "SOLUSD", "XRPUSD", 
                                        "ADAUSD", "BCHUSD", "LTCUSD", "DOTUSD", "TRXUSD", 
                                        "UNIUSD", "VETUSD", "FILUSD", "ICPUSD", "XLMUSD", 
                                        "SUIUSD", "TAOUSD", "WLDUSD", "SKYUSD", "APEUSD"};

    // Initialisation du manageur télémétrique
    TelemetryManager telemetry_manager(symbols);

    ThreadSafeQueue<normalized_data> shared_queue;
    ThreadSafeQueue<History> opt_input_queue;
    ThreadSafeQueue<opt_hawkesParams> opt_output_queue;
    auto binanceWS = std::make_unique<BinanceWS>(symbols, 3, shared_queue);
    auto coinbaseWS = std::make_unique<CoinbaseWS>(symbols, 3, shared_queue);
    auto krakenWS = std::make_unique<KrakenWS>(symbols, 3, shared_queue);
    // auto OkxWS = std::make_unique<OkxWS>(symbols, 3, shared_queue);
    // auto BybitWS = std::make_unique<BybitWS>(symbols, 3, shared_queue);
    int calibration_duration = 10; // seconds
    int training_duration = 30;
    std::vector<std::unique_ptr<GenericWebSocket>> clients;
    // On déplace le pointeur dans le vecteur
    clients.push_back(std::move(binanceWS));
    clients.push_back(std::move(coinbaseWS));
    clients.push_back(std::move(krakenWS));
    // clients.push_back(std::move(OkxWS));
    // clients.push_back(std::move(BybitWS));
    Json::Value websocket_map;
    websocket_map["Binance"] = 0;
    websocket_map["Coinbase"] = 1;
    websocket_map["Kraken"] = 2;
    // websocket_map["Okx"] = 3;
    // websocket_map["Bybit"] = 4;
    int max_workers = 5; // On définit une valeur pour n_max_workers
    Json::Value worker_mapping;

    Json::Value symbols_map;
    for (int i = 0; i < symbols.size(); i++) {
        symbols_map[symbols[i]] = i;
    }

    SchedulerConfig *schelConfig = new SchedulerConfig(); 
    schelConfig->calibration_time = calibration_duration;
    schelConfig->training_time = training_duration;
    schelConfig->n_max_workers = max_workers;
    schelConfig->symbols = symbols;
    schelConfig->websocket_map = websocket_map;
    schelConfig->worker_mapping = worker_mapping;
    schelConfig->symbols_map = symbols_map;

    Json::Value assets_data;
    for (int i = 0; i < symbols.size(); i++) assets_data[symbols[i]] = i;
    
    std::cout << assets_data.toStyledString() << std::endl;
    
    CalibrationEngine calibrationE(shared_queue, assets_data);
    
    Scheduler scheduler(schelConfig, clients, shared_queue, opt_input_queue, opt_output_queue, calibrationE, telemetry_manager);
    std::cout << "Starting System..." << std::endl;

    UserInterface ui(*schelConfig, telemetry_manager);

    // std::thread scheduler_thread([&scheduler](){
    //     scheduler.run();
    // });

    ui.main_renderer();

    //scheduler_thread.join();

    return 0;
}