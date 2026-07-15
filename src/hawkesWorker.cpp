#include "hawkesWorker.h"
#include <iostream>

#include <cmath>
#include <chrono>
#include <cstdint>

extern "C" void SystemLog(const char* fmt, ...);

// Version du format binaire des paramètres optimisés. À incrémenter à chaque
// changement de layout : un ancien fichier .bin est alors rejeté proprement par
// load_optimized_params au lieu d'être mal interprété (lecture désalignée →
// taille aberrante → resize géant / accès hors bornes → segfault).
static const uint32_t PARAMS_FILE_MAGIC = 0x484B5032; // 'HKP2'

// Remplace glfwGetTime() pour mesurer le temps écoulé depuis le démarrage du programme.
// glfwGetTime() nécessite que GLFW soit initialisé au préalable, ce qui n'est pas garanti
// dans les threads workers qui démarrent avant l'UI. std::chrono::steady_clock ne dépend
// d'aucune bibliothèque externe et est sûr à appeler depuis n'importe quel thread.
static double get_time_sec() {
    static auto start = std::chrono::steady_clock::now();
    return std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
}

// Sérialise un std::vector<double> dans un flux binaire : taille puis données brutes.
// On ne peut pas écrire le vecteur directement avec sizeof() car il contient des pointeurs
// internes qui seraient invalides à la relecture (bus error ou UB).
static void write_vector(std::ofstream& f, const std::vector<double>& v) {
    size_t n = v.size();
    f.write(reinterpret_cast<const char*>(&n), sizeof(n));
    f.write(reinterpret_cast<const char*>(v.data()), n * sizeof(double));
}

// Désérialise un std::vector<double> depuis un flux binaire : lit la taille, redimensionne,
// puis lit les données brutes. Symétrique de write_vector.
static bool read_vector(std::ifstream& f, std::vector<double>& v) {
    size_t n = 0;
    f.read(reinterpret_cast<char*>(&n), sizeof(n));
    // Garde-fou : lecture échouée ou taille aberrante = fichier corrompu/incompatible.
    if (!f || n > 100000) return false;
    v.resize(n);
    f.read(reinterpret_cast<char*>(v.data()), n * sizeof(double));
    return static_cast<bool>(f);
}

// Sérialise manuellement chaque champ de opt_hawkesParams dans un fichier binaire.
// opt_hawkesParams contient des std::vector et un std::string dont on ne peut pas
// écrire la représentation mémoire brute (sizeof) : les objets STL embarquent des
// pointeurs vers le tas qui deviendraient invalides à la relecture.
// Format : [status(int)] [sym_len(size_t)] [symbol(chars)] [alpha] [beta] [mu]
void save_optimized_params(const std::string& filename, const opt_hawkesParams& params) {
    std::ofstream file(filename, std::ios::binary);
    if (!file.is_open()) {
        std::cerr << "Error opening file for writing: " << filename << std::endl;
        return;
    }
    file.write(reinterpret_cast<const char*>(&PARAMS_FILE_MAGIC), sizeof(PARAMS_FILE_MAGIC));
    int status = static_cast<int>(params.status);
    file.write(reinterpret_cast<const char*>(&status), sizeof(status));
    // target_dim identifie la dimension optimisée : les params ne concernent qu'une ligne.
    file.write(reinterpret_cast<const char*>(&params.target_dim), sizeof(params.target_dim));
    size_t sym_len = params.symbol.size();
    file.write(reinterpret_cast<const char*>(&sym_len), sizeof(sym_len));
    file.write(params.symbol.data(), sym_len);
    write_vector(file, params.alpha);
    write_vector(file, params.beta);
    // mu et phi sont désormais des scalaires (une valeur par dimension cible).
    file.write(reinterpret_cast<const char*>(&params.mu), sizeof(params.mu));
    file.write(reinterpret_cast<const char*>(&params.phi), sizeof(params.phi));
    file.close();
    std::cout << "Optimized parameters saved to " << filename << std::endl;
}

// Désérialise opt_hawkesParams depuis un fichier binaire. Doit rester synchrone avec
// save_optimized_params : tout changement de format dans l'un invalide les fichiers
// produits par l'autre.
// Renvoie true seulement si le fichier a été lu ET qu'il correspond au format courant.
// Un ancien fichier (mauvais magic) ou tronqué est rejeté sans planter : l'appelant
// doit vérifier la valeur de retour avant d'utiliser params.
bool load_optimized_params(const std::string& filename, opt_hawkesParams& params) {
    std::ifstream file(filename, std::ios::binary);
    if (!file.is_open()) {
        std::cerr << "Error opening file for reading: " << filename << std::endl;
        return false;
    }
    uint32_t magic = 0;
    file.read(reinterpret_cast<char*>(&magic), sizeof(magic));
    if (!file || magic != PARAMS_FILE_MAGIC) {
        std::cerr << "Fichier de paramètres incompatible ou obsolète, ignoré : " << filename << std::endl;
        return false;
    }
    int status = 0;
    file.read(reinterpret_cast<char*>(&status), sizeof(status));
    params.status = static_cast<AlgoState>(status);
    file.read(reinterpret_cast<char*>(&params.target_dim), sizeof(params.target_dim));
    size_t sym_len = 0;
    file.read(reinterpret_cast<char*>(&sym_len), sizeof(sym_len));
    if (!file || sym_len > 4096) return false; // garde-fou anti-corruption
    params.symbol.resize(sym_len);
    file.read(params.symbol.data(), sym_len);
    if (!read_vector(file, params.alpha)) return false;
    if (!read_vector(file, params.beta)) return false;
    file.read(reinterpret_cast<char*>(&params.mu), sizeof(params.mu));
    if (!read_vector(file, params.phi)) return false;
    std::cout << "Optimized parameters loaded from " << filename << std::endl;
    return static_cast<bool>(file);
}

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
    // Si un fichier de paramètres optimisés existe pour un symbol et qu'il est non-vide,
    // on injecte directement alpha/beta/mu dans le modèle. Cela permet de sauter la phase
    // de calibration au démarrage du programme quand des paramètres ont déjà été calculés
    // lors d'une exécution précédente.
    for (int i = 0; i < this->n_assets; i++) {
        std::string symbol = this->workerParam->assets[i];
        std::string filename = "optimized_params_" + symbol + ".bin";
        // ios::ate positionne le curseur en fin de fichier : tellg() donne directement la taille.
        std::ifstream test(filename, std::ios::binary | std::ios::ate);
        if (test.is_open() && test.tellg() > 0) {
            test.close();
            opt_hawkesParams params;
            // Le fichier ne contient que les paramètres d'UNE dimension cible (la dernière
            // optimisée avant l'arrêt). On ne l'injecte que s'il est lu correctement et
            // dimensionné comme attendu (sinon on saute : évite tout accès hors bornes).
            int n = this->workerParam->n_websockets;
            if (load_optimized_params(filename, params)) {
                int td = params.target_dim;
                if (td >= 0 && td < n &&
                    (int)params.alpha.size() == n &&
                    (int)params.beta.size()  == n &&
                    (int)params.phi.size()   == n) {
                    for (int src = 0; src < n; src++) {
                        // Convention du modèle : alpha/beta indexés [source*n + target],
                        // phi indexé [target*n + source] (cf. compute_virtual_intensities).
                        this->models[i].alpha[src * n + td] = params.alpha[src];
                        this->models[i].beta[src * n + td]  = params.beta[src];
                        this->models[i].phi[td * n + src]   = params.phi[src];
                    }
                    this->models[i].mu[td]  = params.mu;
                    // On met à jour le snapshot des paramètres optimisés dans le TelemetryManager pour que l'UI puisse les afficher dès le démarrage.
                    this->models[i].update_hawkes_params(params);
                }
            }
        }
    }
    SystemLog("Initialisation des WORKERS !");
}

// METHODS
void Worker::run() {
    // Execution pendant durée d'entrainement
    std::cout << "Worker " << this->workerParam->worker_id << " en cours d'execution..." << std::endl;
    auto work_duration      = std::chrono::seconds(this->workerParam->training_duration);
    
    // Utilisation de steady_clock pour le contrôle du flux (boucles, chronomètres)
    auto start              = std::chrono::steady_clock::now();
    auto now                = std::chrono::steady_clock::now();
    auto last_optim_time    = std::chrono::steady_clock::now();
    auto OPTIM_INTERVAL     = std::chrono::seconds(10);

    // Utilisation de system_clock pour générer un T_max aligné sur les timestamps UNIX (données)
    auto sys_start          = std::chrono::system_clock::now();
    auto sys_end_point      = sys_start + work_duration;
    
    // Convertir en durée depuis l'origine
    auto duration_since_epoch = sys_end_point.time_since_epoch();
    double rate = 1.0 / 60.0;
    // Convertir en double (en secondes). On a maintenant une grandeur en ~1.77e9
    double T_max = std::chrono::duration<double>(duration_since_epoch).count();
    double last_time = get_time_sec();
    while ((now - start) < work_duration) {
        now = std::chrono::steady_clock::now();

        // std::cout << "Worker " << this->workerParam->worker_id << " : " << (now - start).count() << "s écoulées." << std::endl;
        normalized_data data;
        if (inqueue.try_pop(data)) {
            int index = this->target_symbol_id[data.symbol].asInt();
            this->models[index].residuals_analysis(data); // Analyse résiduelle avant le saut (phi pré-jump, dt correct)
            this->models[index].update_model(data); // Mise à jour des intensités en temps réel
        }
        // Toutes les OPTIM_INTERVAL secondes, on optimise les paramètres
        if ((now - last_optim_time) >= OPTIM_INTERVAL) {
            for (int i = 0; i < this->n_assets; i++) {
                // Au démarrage de l'algorithme, il y a peu de données dans le buffer
                // S'il y en a pas assez, on attendra OPTIM_INTERAVL secondes en plus
                if (this->models[i].n_data > 10) {

                    std::cout << "Envoie de " << this->models[i].n_data << " données au worker d'optimisation..." << std::endl;
                    // Boucle temporaire : une optimisation par dimension cible.
                    // IMPORTANT : chaque History poussée doit posséder SA PROPRE copie profonde
                    // des événements. Le worker d'optimisation fait un delete[] sur history.events
                    // après CHAQUE paquet ; partager un même buffer entre les n dimensions
                    // provoquerait un double free / use-after-free. La copie évite aussi la
                    // race condition avec le buffer du modèle mis à jour en temps réel.
                    for (int dim = 0; dim < this->workerParam->n_websockets; dim++) {
                        Event* events_copy = new Event[this->models[i].n_data];
                        std::copy(this->models[i].buffer.begin(), this->models[i].buffer.end(), events_copy);

                        History history;
                        history.events = events_copy;
                        history.total_events = this->models[i].n_data - 1;
                        history.T_max = T_max;
                        history.symbol = this->workerParam->assets[i].data();
                        history.target_dim = dim;
                        this->opt_input_queue.push(history);
                    }
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
                std::cout << new_params.symbol << std::endl;
                int id = this->target_symbol_id.get(new_params.symbol, -1).asInt();
                // Si le worker ne gère pas le symbol, nous avons alors -1
                if (id == -1) continue;

                // Diffusion des nouveaux paramètres
                int index = this->target_symbol_id[new_params.symbol].asInt();
                // new_params ne contient que les paramètres optimisés pour UNE dimension cible.
                // Convention du modèle (cf. compute_virtual_intensities / residuals_analysis) :
                //   alpha/beta indexés [source*n + target], phi indexé [target*n + source].
                int n = this->workerParam->n_websockets;
                int td = new_params.target_dim;
                for (int src = 0; src < n; src++) {
                    this->models[index].alpha[src * n + td] = new_params.alpha[src];
                    this->models[index].beta[src * n + td]  = new_params.beta[src];
                    this->models[index].phi[td * n + src]   = new_params.phi[src];
                }
                this->models[index].mu[td]      = new_params.mu;

                // Envoie des nouveaux paramètres optimisés à l'UI pour affichage
                this->models[index].update_hawkes_params(new_params);
                // Sauvegarde des paramètres optimisés dans un fichier binaire
                std::string filename = "optimized_params_" + new_params.symbol + ".bin";
                save_optimized_params(filename, new_params);
            } else if (new_params.status == ERROR) {
                std::cout << "Erreur d'optimisation pour l'actif : " << new_params.symbol << std::endl;
            }
        }

        double current_unix = now_unix();
        for (int i = 0; i < this->n_assets; i++) {
            this->models[i].compute_virtual_intensities(current_unix);
        }

        now = std::chrono::steady_clock::now();

        // Déterminer à quel moment cette frame DOIT se terminer
        double target_time = last_time + rate;
        double current_time = get_time_sec();

        // Si on est en avance, on attend
        if (current_time < target_time) {
            double sleep_time = target_time - current_time;
            std::this_thread::sleep_for(std::chrono::duration<double>(sleep_time));
        }

        // Mettre à jour last_time de manière stable (évite le micro-stuttering)
        last_time += rate; 
        // Note: Si l'application a complètement freezé, on reset pour éviter de rattraper le retard en accéléré
        if (get_time_sec() - last_time > 1.0) {
            last_time = get_time_sec();
        }
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
    this->n_data = 0;
    // this->buffer.push_back({0.0, 0}); // Initialisation du buffer avec un event fictif pour éviter les problèmes d'accès à la position 0 avant d'avoir des données réelles

    this->intensities.assign(this->n_websockets, 0.0);
    this->last_global_time = now_unix();
    this->last_time.assign(this->n_websockets, now_unix());
    this->mu.assign(this->n_websockets, 0.0);
    int x = this->n_websockets * this->n_websockets;
    // Théoriquement, alpha et beta sont des matrices, ici, on va géré des vecteurs
    // pour des raisons d'optimisation mémoire.
    this->alpha.assign(x, 0.0);
    this->beta.assign(x, 0.0);
    this->phi.assign(x, 0.0);

    this->compensator.assign(this->n_websockets, 0.0);

    // EWMA des résidus : une valeur par source, non amorcée tant qu'aucun résidu calibré n'est arrivé
    this->ewma_residuals.assign(this->n_websockets, 0.0);
    this->ewma_initialized.assign(this->n_websockets, false);

    this->branching_matrix.assign(x, 0.0); // Initialisation de la matrice de branchement à zéro
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

std::vector<double> HawkesModel::compute_virtual_intensities(double current_time) const {
    int n = this->n_websockets;
    std::vector<double> virtual_intensities(n, 0.0);

    double dt = (current_time - this->last_global_time);
    if (dt < 0.0) dt = 0.0;

    for (int i = 0; i < n; i++) {
        double excitation = 0.0;
        
        for (int j = 0; j < n; j++) {
            int index = coord2index(j, i, n);
            // On projette le decay dans une variable LOCALE, on ne touche pas à this->phi
            double decayed_phi = this->phi[i*n+j] * std::exp(-this->beta[index] * dt);
            
            excitation += this->alpha[index] * decayed_phi;
        }
        
        virtual_intensities[i] = std::max(0.0, this->mu[i] + excitation);
    }
    this->telemetry_manager.update(this->symbol_id, virtual_intensities, get_time_sec());
    return virtual_intensities;
}


void HawkesModel::update_model(normalized_data data) {
    std::string exchange = data.exchange;
    int source_idx = this->websocket_map[exchange].asInt();
    double now = data.timestamp;
    double dt = now - this->last_global_time;
    
    // Protection HFT : les paquets UDP/Websockets peuvent arriver dans le désordre
    if (dt < 0.0) {
        dt = 0.0; 
    }

    // DECAY (Décroissance de la mémoire)
    int n = this->n_websockets;
    if (dt > 0.0) {
        for (int i = 0; i < n; i++) {
            for (int j = 0; j < n; j++) {
                int index = coord2index(j, i, n);
                // Utilisation de std::exp (bonne pratique C++)
                this->phi[index] *= std::exp(-this->beta[index] * dt);
            }
        }
    }

    // CALCUL DE L'INTENSITÉ
    for (int i = 0; i < n; i++) {
        double excitation = 0.0;
        for (int j = 0; j < n; j++) {
            int index = coord2index(j, i, n);
            excitation += this->alpha[index] * this->phi[i*n+j];
        }
        this->intensities[i] = std::max(0.0, this->mu[i] + excitation);
    }

    // JUMP (Excitation suite au nouvel événement)
    for (int i = 0; i < n; i++) {
        // En C++, il vaut mieux ajouter 1.0 explicite pour les doubles
        this->phi[coord2index(source_idx, i, n)] += 1.0; 
    }

    // MISE À JOUR DES TEMPS
    this->last_global_time = now; // Remplace la logique du std::max_element
    this->last_time[source_idx] = now; // Gardé si vous l'utilisez ailleurs

    // BUFFER ET TÉLÉMÉTRIE
    Event event = {data.timestamp, source_idx};
    this->buffer.push_back(event);
    this->n_data++;

    if (this->n_data > 50000) {
        int prune_count = 10000;
        this->buffer.erase(this->buffer.begin(), this->buffer.begin()+prune_count);
        this->n_data -= prune_count;
    }
}

void HawkesModel::residuals_analysis(normalized_data data) {
    std::string exchange = data.exchange;
    int source_idx = this->websocket_map[exchange].asInt();
    
    double now = data.timestamp;

    // Remplacez votre std::max_element par une variable de classe "this->last_global_time"
    double dt = now - this->last_global_time;
    
    // Protection HFT : les paquets UDP/Websockets peuvent arriver dans le désordre
    if (dt < 0.0) {
        dt = 0.0; 
    }

    double diff_compensator = this->mu[source_idx] * dt;
    for (int j = 0; j < this->n_websockets; j++) {
        double alpha_ji = this->alpha[coord2index(j, source_idx, this->n_websockets)];
        double beta_ji = this->beta[coord2index(j, source_idx, this->n_websockets)];
        double phi_ij = this->phi[coord2index(source_idx, j, this->n_websockets)];
        // Limite de L'Hôpital quand beta -> 0 : (alpha/beta)*(1-exp(-beta*dt)) -> alpha*dt
        // On utilise cette forme pour éviter les divisions par zéro lorsque beta est très petit.
        if (std::abs(beta_ji) < 1e-12) {
            diff_compensator += alpha_ji * dt * phi_ij;
        } else {
            diff_compensator += (alpha_ji / beta_ji) * (1.0 - std::exp(-beta_ji * dt)) * phi_ij;
        }
    }
    this->compensator[source_idx] += diff_compensator;

    // N'exploite les résidus qu'après calibration (au moins un β > 0) : avant, les
    // paramètres sont nuls et le compensateur n'a pas de signification statistique.
    bool calibrated = std::any_of(this->beta.begin(), this->beta.end(),
                                  [](double b){ return b > 1e-10; });
    if (calibrated && diff_compensator > 0.0) {
        // Mise à jour incrémentale de l'EWMA des résidus pour la source concernée.
        // Pour un modèle bien ajusté, les résidus suivent une loi Exp(1) (moyenne 1) :
        // l'EWMA offre une lecture temps réel de la qualité du modèle, censée osciller
        // autour de 1. On l'amorce sur le premier résidu observé pour éviter le biais
        // initial vers 0 d'une EWMA partant de zéro.
        double& ewma = this->ewma_residuals[source_idx];
        if (!this->ewma_initialized[source_idx]) {
            ewma = diff_compensator;
            this->ewma_initialized[source_idx] = true;
        } else {
            ewma = (1.0 - this->ewma_alpha) * ewma + this->ewma_alpha * diff_compensator;
        }
        this->telemetry_manager.update_residuals_analysis(this->symbol_id, source_idx, diff_compensator, ewma);
    }
}

void HawkesModel::update_hawkes_params(const opt_hawkesParams& new_params) {
    int size_alpha = this->alpha.size();
    int size_beta = this->beta.size();
    if (size_alpha != size_beta) {
        std::cerr << "Error: alpha and beta vectors must have the same size." << std::endl;
        return;
    }
    for (int i = 0; i < size_alpha; ++i) {
        if (this->beta[i] > 1e-12) { // Avoid division by zero
            this->branching_matrix[i] = this->alpha[i] / this->beta[i];
        } else {
            this->branching_matrix[i] = -1.0; 
        }
    }
    this->telemetry_manager.update_parameters_snapshot(this->symbol_id, new_params, this->branching_matrix);
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
    this->optimizerConfig.max_iter = 4000;
    this->optimizerConfig.rho = 1.0;
    this->optimizerConfig.chi = 2.0;
    this->optimizerConfig.psi = 0.5;
    this->optimizerConfig.sigma = 0.5;
    this->optimizerConfig.n_dim = n_websockets; // Dimension runtime propagée à l'optimiseur C

    this->n_websockets = n_websockets;

    // Définitions des espaces de recherches
    int n_params_local = 1+ 2 * this->n_websockets;
    std::vector<param_bounds> bounds(n_params_local);
    bounds[0].min = 0.01;
    bounds[0].max = 2.0;
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
            ModelParams *params = hawkes_model_optim(&history, &this->optimizerConfig, history.target_dim);
            if (params == nullptr) {
                // target_dim hors bornes : hawkes_model_optim a renvoyé NULL. On ignore
                // ce paquet et on libère les événements copiés pour ne pas fuir.
                std::cerr << "Optimisation ignorée : target_dim invalide (" << history.target_dim << ")." << std::endl;
                delete[] history.events;
                continue;
            }
            std::cout << "Lancement de l'optimisation de Nelder-Mead" << std::endl;
            // Adaptation des données recu pour le code c++
            // /!\ On copie juste les données
            // Comme la tailles des listes recu sont de taille fixe, la complexité est considéré constante ici.
            opt_hawkesParams formated_output_params;
            formated_output_params.alpha.assign(params->alpha, params->alpha + this->n_websockets);
            formated_output_params.beta.assign(params->beta, params->beta + this->n_websockets);
            formated_output_params.mu = params->mu; // Copie directe du double
            formated_output_params.phi.assign(params->phi, params->phi + this->n_websockets);
            // Pour le moment le status est OK, mais voir pour d'autre conditions pour envoyer des cas d'erreur ou de problème.
            formated_output_params.status = OK;
            formated_output_params.symbol = history.symbol; // Associe le symbol du modèle
            formated_output_params.target_dim = params->target_dim; // Associe la dimension optimisée

            // Les données sont copiées dans formated_output_params : on libère la structure C
            // (allouée à chaque appel de hawkes_model_optim, une fois par dimension cible).
            free(params->alpha);
            free(params->beta);
            free(params->phi);
            free(params);

            delete[] history.events; // Libère la mémoire alloué pour les événements du history
            // Push dans la queue thread safe qui lie ce worker aux autres worker gérant les modèles
            // Les modèles vérifie que les paramètres sont bien à eux avant de les retirer.
            this->opt_output_queue.push(formated_output_params); 
        }
    }
}

void Worker::str_new_params(std::vector<double> alpha,std::vector<double> beta,std::vector<double> mu ) {
    std::cout << "Valeurs alpha :" << std::endl;
    for (auto & a : alpha) {
        std::cout << a << " ";
    }
    std::cout << std::endl;
    std::cout << "Valeurs beta :" << std::endl;
    for (auto & a : beta) {
        std::cout << a << " ";
    }
    std::cout << std::endl;
    std::cout << "Valeurs mu :" << std::endl;
    for (auto & a : mu) {
        std::cout << a << " ";
    }
    std::cout << std::endl;
}