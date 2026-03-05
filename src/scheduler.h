#ifndef SCHEDULER_H
#define SCHEDULER_H

#pragma once
#include <json/value.h>
#include <json/json.h>
#include <thread>
#include <fstream>
#include <filesystem>
#include <iostream>
#include <cassert>

#include "struct.h"
#include "genericWS.h"
#include "calibration/calibration.h"
#include "calibration/hawkes_model/include/struct.h"

/** The scheduler is a module that receives data from different 
sources, and redispatches them to different models. In short, 
it manages data reception, parallelization, calibration, and data sorting. 
Similarly, it is the brain of the software.  */

/* The scheduler will be initialized at first to configure the multi core and multi thread
system. We should consider it like an object / module of the algorithme.
The time initialization of the scheduler could be long, because it will looking for a first calibration,
to dispatch correctly the different models. We could desactivate this fisrt calibration, if we give a 
configuration file (json) which contain a mapping. */

class Scheduler {
    protected:
        SchedulerConfig* config;

        ThreadSafeQueue<normalized_data>& inqueue;
        ThreadSafeQueue<History>& opt_input_queue;
        ThreadSafeQueue<opt_hawkesParams>& opt_output_queue;
        std::vector<std::unique_ptr<GenericWebSocket>>& clients;
        CalibrationEngine& calibration;
        TelemetryManager& telemetry_manager;
        
        // Logic cores for calibration
        int ncores = std::thread::hardware_concurrency();  // Number of CPU cores available, equal 0 when not able to be determined
        int N_SECURITY_MARGIN = 2; // Security margin to avoid overloading the CPU
        int available_cores = std::max(ncores - N_SECURITY_MARGIN, 1); // Number of cores available for calibration
        // By default, use all available cores. The user can change the limit in the constructor.
        int n_max_workers = available_cores; 
        SchedulerState state;
        Json::Value map;
    
    public:
        int scheduler_state;

        Scheduler(SchedulerConfig* config, 
            std::vector<std::unique_ptr<GenericWebSocket>>& clients, 
            ThreadSafeQueue<normalized_data>& q,
            ThreadSafeQueue<History>& opt_input_queue,
            ThreadSafeQueue<opt_hawkesParams>& opt_output_queue,
            CalibrationEngine& calibration,
            TelemetryManager& telemetry_manager
        );

        /**
         * @brief Résoud le problème du Bin packing pour répartir de facon homogène, la charge de travail des différents workers.
         * 
         * La résolution de se problème fait appel à une méthode greedy, elle opte pour une résolution rapide et approximative. Même si le résultat n'est pas l'optimal, la répartition dans chaques cores est convenable.
         * 
         * @param assets_volume Un vecteur d'entiers indiquant le nombre de données recu pour chaque assets. Nous pouvous utiliser la variable websocket_map de la structure de configuration pour connaître à quel asset est associé l'entier.
         */
        Json::Value greedy_cores_packing(std::vector<volume_symbol> assets_volume);

        /**
         * 
         */
        void run();
};

#endif // SCHEDULER_H