#include "scheduler.h"
#include "struct.h"
#include <cstdlib>
#include "hawkesWorker.h"

// ---=== CONSTRUCTOR ===---
Scheduler::Scheduler(SchedulerConfig* config, 
    std::vector<std::unique_ptr<GenericWebSocket>>& clients, 
    ThreadSafeQueue<normalized_data>& q,
    ThreadSafeQueue<History>& opt_input_queue,
    ThreadSafeQueue<opt_hawkesParams>& opt_output_queue,
    CalibrationEngine& calibration,
    TelemetryManager& telemetry_manager
) : 
    inqueue(q), 
    opt_input_queue(opt_input_queue),
    opt_output_queue(opt_output_queue),
    clients(clients),
    calibration(calibration),
    telemetry_manager(telemetry_manager) {
    // The user can choose the maximum number of workers (threads) to use for calibration.
    // By default, use all available cores minus a security margin.
    // We need to check that n_max_workers choosen by the user is possible.
    if (config->n_max_workers > 0 && config->n_max_workers <= available_cores) {
        this->n_max_workers = config->n_max_workers;
    }

    // this->scheduler_state = IDLE;

    // If there is not a worker mapping, change the state to calibrating
    if (!config->worker_mapping) this->scheduler_state = CALIBRATING;

    this->config = config;
}

typedef struct {
    std::vector<std::string> assets; // List of symbols
    int i; // index of the heap
    int volume; // Number of data received from the list of symbols
} heap_param;

/**
 * Structure contenant la fonction de comparaison entre deux noeuds du tas binaire.
 */
struct CompareWorker {
    bool operator()(const heap_param& a, const heap_param& b) const {
        return a.volume > b.volume; 
    }
};

// Comparateur pour le tri des assets (du plus gros au plus petit)
bool compare_assets_desc(const volume_symbol& a, const volume_symbol& b) {
    return a.volume > b.volume;
}

// ---=== METHODS ===---

Json::Value Scheduler::greedy_cores_packing(std::vector<volume_symbol> assets_volume) {
    // Trie de assets_volume par rapport aux valeurs clés dans l'ordre décroissant (volume)
    std::sort(assets_volume.begin(), assets_volume.end(), compare_assets_desc);
    // Création du tas binaire
    std::priority_queue<heap_param, std::vector<heap_param>, CompareWorker> workers_heap;

    // Initialisation des workers (vides au début)
    std::cout << "Nombre max de workers : " << this->n_max_workers << std::endl;
    for (int i = 0; i < this->n_max_workers; i++) {
        heap_param worker;
        worker.i = i;
        worker.volume = 0;
        // Ajout du noeud dans le tas binaire
        workers_heap.push(worker);
    }

    for (const auto& asset : assets_volume) {
        // a. On récupère le worker le MOINS chargé (au sommet du tas)
        heap_param current_worker = workers_heap.top();
        // b. On le retire temporairement du tas
        workers_heap.pop();
        
        current_worker.assets.push_back(asset.symbol);
        current_worker.volume += asset.volume;
        
        workers_heap.push(current_worker);
    }

    Json::Value mapping_table;
    while (!workers_heap.empty()) {
        heap_param w = workers_heap.top();
        workers_heap.pop();
        
        // Exemple de remplissage Json
        for(const auto& asset_name : w.assets) {
            mapping_table[asset_name] = w.i;
        }
    }

    return mapping_table;
}

void Scheduler::run() {
    this->state = INITIALIZATION;
    // Le vecteur websocketThreads stock les threads de chaque websocket
    // On les lancera en concurentielle sur un coeur physique
    std::vector<std::thread> websocketThreads(this->clients.size());
    // Lancement des webscockets sur un thread
    for (int i = 0; i < this->clients.size(); i++) {
        websocketThreads.emplace_back([this, i]() {
            std::cout << "Connecting to Websocket " << this->config->symbols[i] << "..." << std::endl;
            this->clients[i]->connect();
        });
    }

    // Lancement de la calibration sur un autre thread
    // Cette calibration ne ralentit pas la réception des données
    std::thread calibrationThread([this]() {
        std::cout << "Starting Calibration Engine..." << std::endl;
        std::vector<volume_symbol> assets_volume = this->calibration.evaluate_assets_volumes(this->config->calibration_time);
        std::cout << "Fin de la calibration" << std::endl;
        // for (volume_symbol volume: assets_volume) std::cout << volume.volume << std::endl;
        // Résolution greedy du worker packing / Bin packing
        // Afin que la quantité de travail entre chaque workers soit homogènes
        this->map = this->greedy_cores_packing(assets_volume);
        std::cout << this->map.toStyledString() << std::endl;
    });
    
    // On termine la calibration d'initialisation
    calibrationThread.join();

    // On crée les listes des symbols associé à chaque worker
    std::vector<std::vector<std::string>> temp_worker_map(this->n_max_workers);
    for (auto symbol : this->config->symbols) {
        int index = this->map[symbol].asInt();
        temp_worker_map[index].push_back(symbol);
    }
    
    // Création des workers
    std::vector<std::thread> workers;
    workers.reserve(this->n_max_workers+1);
    // n_max_workers workers sont créés
    int i = 0;
    for (; i < this->n_max_workers; i++) {
        // Création des paramètres du worker
        auto workerParam = std::make_unique<WorkerParam>();
        workerParam->websocket_map    = this->config->websocket_map;
        workerParam->symbols_map      = this->config->symbols_map;
        workerParam->assets           = temp_worker_map[i];
        workerParam->n_websockets     = this->clients.size();
        workerParam->training_duration = this->config->training_time;
        workerParam->worker_id        = i;

        // Création de la tâche sur un thread
        workers.emplace_back([this, p = std::move(workerParam)]() {
            // CONFIGURATION DU THREAD (Dès le début)
            // On demande au système des ressources "Performance" (P-Cores)
            setCurrentThreadPriority();
            // Optionnel : Nommer le thread pour le débogage dans Instruments
            char threadName[16];
            snprintf(threadName, sizeof(threadName), "Worker-%d", p->worker_id);
            pthread_setname_np(threadName);

            // INSTANCIATION & RUN
            Worker worker(p.get(), this->inqueue, this->opt_input_queue, this->opt_output_queue, this->telemetry_manager);
            worker.run();
        });

        std::cout << "Worker " << i << " lancé!" << std::endl;
    }
    // Création d'un worker pour l'optimisation/calcul intensif
    workers.emplace_back([this, i]() {
        setCurrentThreadPriority(); // On dit que ce worker est d'ordre prioritaire.
        char threadName[16];
        snprintf(threadName, sizeof(threadName), "Worker-%d", i);
        pthread_setname_np(threadName);
        // Initialisation de la classe qui va envoyer appeller les fonctions C pour les optimisations.
        HawkesOptimizer optimizer(this->opt_input_queue, this->opt_output_queue, this->clients.size());
        optimizer.run();
    });
    std::cout << "Worker d'optimisation lancé !" << std::endl;

    // On termines tout les workers dont l'optimiseur
    for (auto& t : workers) {
        if (t.joinable()) t.join();
    }
    // On ferme les connections et on arrête le thread gérant les websockets.
    for (auto& client : websocketThreads) {
        client.join();
    }
}