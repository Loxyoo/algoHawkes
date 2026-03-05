#include "hawkesWorker.h"
#include <iostream>

#include <cmath>

// --------------------------
// ---=== WORKER CLASS ===---
// --------------------------

// CONSTRUCTOR

Worker::Worker(
    WorkerParam* workerParam,
    ThreadSafeQueue<normalized_data>& inqueue,
    ThreadSafeQueue<History>& opt_input_queue,
    ThreadSafeQueue<opt_hawkesParams>& opt_output_queue,
    TelemetryManager& telemetry_manager
) :
    // Thread/Core Safe Shared queue
    inqueue(inqueue),
    opt_input_queue(opt_input_queue),
    opt_output_queue(opt_output_queue),
    telemetry_manager(telemetry_manager)
{
    this->workerParam = workerParam;
    this->n_assets = this->workerParam->assets.size();
    // Réserve l'espace mémoire pour les models
    this->models.reserve(this->n_assets);
    for (int i = 0; i < this->n_assets; i++) {
        std::string symbol = this->workerParam->assets[i];
        // Initialisation de chaque modeles
        models.emplace_back(
            this->workerParam->worker_id, 
            this->workerParam->n_websockets, 
            symbol, 
            inqueue, 
            this->workerParam->training_duration, 
            this->workerParam->websocket_map,
            this->telemetry_manager,
            this->workerParam->symbols_map[symbol].asInt()
        );
        this->target_symbol_id[symbol] = i;
    }
}

// METHODS
void Worker::run() {
    // Execution pendant durée d'entrainement
    std::cout << "Worker " << this->workerParam->worker_id << " en cours d'execution..." << std::endl;
    auto work_duration      = std::chrono::seconds(this->workerParam->training_duration);
    auto start              = std::chrono::steady_clock::now();
    auto now                = std::chrono::steady_clock::now();
    auto last_optim_time    = std::chrono::steady_clock::now();
    auto OPTIM_INTERVAL     = std::chrono::seconds(10);
    // Calculer le moment de fin (time_point)
    auto end_point = start + work_duration;
    // Convertir en durée depuis l'origine (time_since_epoch)
    auto duration_since_epoch = end_point.time_since_epoch();
    // Convertir en double (en secondes)
    // On force la conversion en 'duration<double>' pour avoir les décimales, puis .count() extrait le nombre
    double T_max = std::chrono::duration<double>(duration_since_epoch).count();
    while ((now - start) < work_duration) {
        // std::cout << "Worker " << this->workerParam->worker_id << " : " << (now - start).count() << "s écoulées." << std::endl;
        normalized_data data;
        if (! this->inqueue.try_pop(data))
            continue;
        // Mise à jour du modèle de hawkes associé au symbol data
        int index = this->target_symbol_id[data.symbol].asInt();
        this->models[index].update_model(data);
        // Toutes les OPTIM_INTERVAL secondes, on optimise les paramètres
        if ((now - last_optim_time) >= OPTIM_INTERVAL) {
            for (int i = 0; i < this->n_assets; i++) {
                // Au démarrage de l'algorithme, il y a peu de données dans le buffer
                // S'il y en a pas assez, on attendra OPTIM_INTERAVL secondes en plus
                if (this->models[i].n_data > 10) {
                    std::cout << "Envoie de " << this->models[i].n_data << " données au worker d'optimisation..." << std::endl;
                    // Envoie au worker HPC des nouvelles données recu pour le symbol associé au modèle
                    History history;
                    history.events = this->models[i].buffer.data();
                    history.total_events = this->models[i].n_data;
                    history.T_max = T_max;
                    this->opt_input_queue.push(history);
                } else {
                    std::cout << "Not enough data to optimize." << std::endl;
                }
                last_optim_time = now;
            }
        }
        // Récupération des paramètres optimisés.
        opt_hawkesParams new_params;
        while (this->opt_output_queue.try_pop(new_params)) {
            // On s'assure que l'état de l'optimisation va bien avec le OK
            if (new_params.status == OK) {
                // Récupère l'indice associé au symbol du paquet recu
                // afin de trouver le bon modèle associé
                int id = this->target_symbol_id.get(new_params.symbol, -1).asInt();
                // Si le worker ne gère pas le symbol, nous avons alors -1
                if (id == -1) continue;
                // Diffusion des nouveaux paramètres
                int index = this->target_symbol_id[new_params.symbol].asInt();
                this->models[index].alpha   = new_params.alpha;
                this->models[index].beta    = new_params.beta;
                this->models[index].mu      = new_params.beta;
            } else if (new_params.status == ERROR) {
                std::cout << "Erreur d'optimisation pour l'actif : " << new_params.symbol << std::endl;
            }
        }
        now = std::chrono::steady_clock::now();
    }
    std::cout << "Execution terminé! " << this->workerParam->worker_id << std::endl;
}

// --------------------------------
// ---=== HAWKES MODEL CLASS ===---
// --------------------------------

HawkesModel::HawkesModel(
    int worker_id,
    int n_websockets,
    std::string asset,
    ThreadSafeQueue<normalized_data>& inqueue,
    int training_duration,
    Json::Value websocket_map,
    TelemetryManager& telemetry_manager,
    int symbol_id
) : 
    // Thread Safe shared queue
    inqueue(inqueue),
    telemetry_manager(telemetry_manager)
{
    this->worker_id = worker_id;
    this->n_websockets = n_websockets;
    this->asset = asset;
    this->training_duration = training_duration;
    this->websocket_map = websocket_map;
    this->symbol_id = symbol_id;

    this->intensities.assign(this->n_websockets, 0.0);
    this->last_time.assign(this->n_websockets, std::chrono::steady_clock::now());
    this->mu.assign(this->n_websockets, 0.0);
    int x = this->n_websockets * this->n_websockets;
    // Théoriquement, alpha et beta sont des matrices, ici, on va géré des vecteurs
    // pour des raisons d'optimisation mémoire.
    this->alpha.assign(x, 0.0);
    this->beta.assign(x, 0.0);

    // INITIALISATION DES VARIABLES POUR L'ARCHITECTURE FPGA
    this->phi.assign(x, 0.0);
}

// METHODS

/**
 * @brief Convertie une coordonnée d'une matrice carré en un indice
 * 
 * Soit (i, j) les coordonnées d'un coefficient de la matrice carré M de dimension nxn. 
 * Et considérons sa matrice applatie M' de taille 1x(nxn), alors la nouvelle 
 * coordonnée convertie dans la matrice M' est i*n+j.
 * 
 * @param i : int row number
 * @param j : int col number
 * @param n : int dim of the squarred matrix
 * 
 * @return int
 */
static int coord2index(int i, int j, int n) {
    if (i < 0 || j < 0 || i >= n || j >= n) {
        std::fprintf(stderr, "Error index in coord2index (%d, %d), for : %d.", i, j, n);
    }
    return i*n+j;
}

void HawkesModel::update_model(normalized_data data) {
    std::string exchange = data.exchange;
    
    auto now = std::chrono::steady_clock::now();

    // *std::max_element because it return an iterator and we want a value
    auto prev_global_time = *std::max_element(
        this->last_time.begin(), this->last_time.end());
    if (std::chrono::duration_cast<std::chrono::milliseconds>(now - prev_global_time) <= std::chrono::milliseconds(0)) {
        prev_global_time = now;
    }

    double dt = std::chrono::duration_cast<std::chrono::milliseconds>(now - prev_global_time).count();
    // Update de l'excitation phi
    int n = this->n_websockets;
    if (dt > 0) {
        for (int i = 0; i < n; i++) {
            for (int j = 0; j < n; j++) {
                int index = coord2index(i, j, n);
                this->phi[index] = exp(-this->beta[index] * dt);
            }
        }
    }

    // Calcul de l'intensité instantannée
    for (int i = 0; i < this->n_websockets; i++) {
        int excitation = 0;
        for (int k = i*n; k < i*(n+1); k++) {
            excitation += this->alpha[k] * this->phi[k];
        }
        this->intensities[i] = this->mu[i] + excitation;
    }

    // Update de phi selon la formule en ajoutant le +1
    int source_idx = this->websocket_map[exchange].asInt();
    for (int i = 0; i < n; i++) {
        this->phi[coord2index(i, source_idx, n)] += 1;
    }

    // Mise à jour du temps
    this->last_time[source_idx] = now;

    // Ajout de la donnée au buffer
    Event event = {data.arrival_time, this->websocket_map[exchange].asInt()};
    this->buffer.push_back(event);
    this->n_data++;

    // Mise à jour de la télémétrie
    this->telemetry_manager.update(this->symbol_id, this->intensities, data.arrival_time);
}

// ------------------------------------
// ---=== HAWKES OPTIMIZER CLASS ===---
// ------------------------------------

HawkesOptimizer::HawkesOptimizer(
    ThreadSafeQueue<History>& opt_input_queue,
    ThreadSafeQueue<opt_hawkesParams>& opt_output_queue,
    int n_websockets
) : 
    opt_input_queue(opt_input_queue), 
    opt_output_queue(opt_output_queue) 
{
    // Paramètres par défaut de l'optimisation de Nelder Mead
    this->optimizerConfig.max_iter = 2000;
    this->optimizerConfig.rho = 1.0;
    this->optimizerConfig.chi = 2.0;
    this->optimizerConfig.psi = 0.5;
    this->optimizerConfig.sigma = 0.5;

    this->n_websockets = n_websockets;

    // Définitions des espaces de recherches
    int n_params_local = 1+ 2 + this->n_websockets;
    std::vector<param_bounds> bounds(n_params_local);
    bounds[0].min = 0.01;
    bounds[0].max = 0.5;
    for (int i = 1; i < n_params_local; i++) {
        bounds[i].min = 0.0;
        bounds[i].max = 10;
    }
    this->optimizerConfig.bounds = bounds.data();
}

void HawkesOptimizer::run() {
    std::cout << "Optimisation Worker : Démarré et en attente." << std::endl;
    while (true) {
        History history;
        while (this->opt_input_queue.try_pop(history)) {
            ModelParams *params = hawkes_model_optim(&history, &this->optimizerConfig);
            std::cout << "Lancement de l'optimisation fiscale" << std::endl;
            // Adaptation des données recu pour le code c++
            // On copie juste les données
            // Comme la tailles des listes recu sont de taille fixe, la complexité est considéré constante ici.
            opt_hawkesParams formated_output_params;
            int n_params_local = 1 + 2 * this->n_websockets;
            formated_output_params.alpha.assign(params->alpha, params->alpha + n_params_local);
            formated_output_params.beta.assign(params->beta, params->beta + n_params_local);
            formated_output_params.mu.assign(params->mu, params->mu + this->n_websockets);
            // Pour le moment le status est OK, mais voir pour d'autre conditions pour envoyer des cas d'erreur ou de problème.
            formated_output_params.status = OK;
            formated_output_params.symbol = history.symbol; // Associe le symbol du modèle
            // Push dans la queue thread safe qui lie ce worker aux autres worker gérant les modèles
            // Les modèles vérifie que les paramètres sont bien à eux avant de les retirer.
            this->opt_output_queue.push(formated_output_params); 
        }
    }
}