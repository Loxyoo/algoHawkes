#ifndef WORKER_H
#define WORKER_H

#include <json/value.h>
#include <json/json.h>
#include <fstream>
#include <algorithm>

#include <chrono>
using namespace std::chrono_literals;
using TimePoint = std::chrono::steady_clock::time_point;

#include "tools.h"
#include "struct.h"
#include "calibration/hawkes_model/include/optimization.h"
#include "calibration/hawkes_model/include/struct.h"

// Fonction qui permet la sauvegarde des paramètres optimisés pour les modèles de Hawkes
void save_optimized_params(const std::string& filename, const opt_hawkesParams& params);

// Fonction qui permet de charger les paramètres optimisés pour les modèles de Hawkes.
// Renvoie false si le fichier est absent, obsolète (mauvais magic) ou corrompu :
// dans ce cas params n'est pas exploitable et ne doit pas être injecté.
bool load_optimized_params(const std::string& filename, opt_hawkesParams& params);


class HawkesModel {
    protected:
        int worker_id; // Identifiant du worker
        // Si n_websockets = 5, un modèle de Hawkes se basera sur 5 processus auto-excitante
        int n_websockets; // Nombre de websockets actif
        std::string asset; // Nom de l'asset géré par ce model
        ThreadSafeQueue<normalized_data>& inqueue; // Queue Thread Safe qui permet de recevoir les données des marchés normalisé
        int training_duration; // Durée de l'entrainement
        Json::Value websocket_map; // Map qui associe le nom d'un exchange à un integer (id)
        TelemetryManager& telemetry_manager; // Telemetry manager pour monitorer les performances et la santé du système
        bool parameters_optimized = false;
        int symbol_id; // Identifiant du symbol géré par ce modèle, associé à un index dans la queue de télémétrie
        double last_global_time; // Variable pour stocker le temps global maximum (remplace la logique du std::max_element)
        std::vector<double> branching_matrix; // Matrice de branchement pour modéliser les interactions entre websockets
        std::vector<residual_circular_buffer> residual_buffers; // Buffers circulaires pour stocker les résidus par source, pour l'analyse des résidus
    public:
        int n_data; // Number of normalized data in buffer
        std::vector<double> intensities; // Vecteur stockant les intensités de Hawkes de chaque websockets
        // std::vector<double> base_intensity;
        std::vector<double> last_time;
        std::vector<double> alpha; // Paramètres de force pour chaques websockets
        std::vector<double> beta; // Paramètres d'oublie pour chaques websockets
        std::vector<double> mu; // Intensités de fonf pour chaques websockets
        
        std::vector<double> phi; // Variable pour l'ARCHITECTURE FPGA
        std::vector<double> compensator; // Variable pour stocker le compensateur de Hawkes pour l'analyse résiduelle

        double ewma_alpha = 0.2; // Paramètre de lissage de l'Exponential Weighted Moving Average (EWMA) des résidus
        std::vector<double> ewma_residuals; // Moyenne mobile exponentielle des résidus, une valeur par source, pour l'analyse temps réel de la qualité du modèle
        std::vector<bool> ewma_initialized; // Indique, par source, si l'EWMA a déjà été amorcée sur un premier résidu (évite le biais initial vers 0)
        
        std::vector<Event> buffer; // Buffer qui enregistre les events, i.e. les timestamps de chaque nouvelle données du symbol gérès par ce model.

        /**
         * @param worker_id identifiant du worker
         * @param n_websockets Nombre de webscokets actif
         * @param asset Symbol que gère ce modèle
         * @param inqueue Thread Safe Queue qui permet de récupérer les données normalisées
         * @param training_duration Durée de l'entrainement
         * @param websocket_map Dictionnaire associé à chaque nom de websockets, un identifiant (entier dans [0:n_websocket-1])
         */
        HawkesModel(
            int worker_id,
            int n_websockets,
            std::string asset,
            ThreadSafeQueue<normalized_data>& inqueue,
            int training_duration,
            Json::Value websocket_map,
            TelemetryManager& telemetry_manager,
            int symbol_id
        );

        std::vector<double> compute_virtual_intensities(double dt) const;
        /**
         * @brief update the intensities in real time
         * 
         * Cette méthode met à jour en temp réel l'intensité de Hawkes. Elle optimise ces paramètres
         * dynamiquement afin de garder une stabilité et une cohérence face aux régimes changeant des marchés.
         * 
         * @param data : Donnée recu qui a été normalisé
         */
        void update_model(normalized_data data);

        /**
         * @brief Analyse des résidus pour évaluer la qualité de l'ajustement du modèle de Hawkes aux données observées.
         * 
         * Cette méthode permet d'examiner les résidus (différences entre les intensités prédites par le modèle et 
         * les événements observés) pour identifier les éventuelles lacunes du modèle, détecter des patterns non capturés,
         * et évaluer la pertinence des paramètres optimisés. L'analyse des résidus est essentielle pour valider la performance 
         * du modèle de Hawkes et guider d'éventuelles améliorations. Lorsque les résidus dérivent significativement de zéro, 
         * cela peut indiquer que le modèle ne capture pas correctement la dynamique des événements, suggérant ainsi la nécessité 
         * d'un rajustement des paramètres ou d'envisager des extensions du modèle.
         */
        void residuals_analysis(normalized_data data);

        void update_hawkes_params(const opt_hawkesParams& new_params);
};

// Classe qui permet de gérer les métriques d'un asset. Comme les variations de prix, les volumes, les intensités de Hawkes, etc. Elle est utilisée pour monitorer la santé du marché et détecter des anomalies.
class AssetMetrics {
    protected:
        std::string asset; // Nom de l'asset
        double time_window; // Fenêtre de temps pour le calcul des métriques
        std::vector<double> price_history; // Historique des prix pour le calcul de la
        double variation; // Variation du prix sur la fenêtre de temps
    
};

class Worker {
    protected:
        ThreadSafeQueue<normalized_data>& inqueue; // Queue Thread Safe contenant les données standardisés
        ThreadSafeQueue<History>& opt_input_queue; // Queue Cores Safe qui permet d'envoyer l'information au worker HPC
        ThreadSafeQueue<opt_hawkesParams>& opt_output_queue; // Queue Cores Safe qui permet d'énvoyer les nouveaux paramètres optimisés
        TelemetryManager& telemetry_manager; // Telemetry manager pour monitorer les performances et la santé du système
        WorkerParam* workerParam; 
        int n_assets; // Nombre d'assets gérés par ce worker
        Json::Value target_symbol_id; // Association d'un index à chaques assets géré [0, n_assets-1]
        std::vector<HawkesModel> models; // Liste des modèles de Hawkes qui seront géré par ce worker
    public: 
        /**
         * @param workerParam Structure de configuration pour l'initialisation du worker. Contient, les paramètres nécessaires.
         * @param inqueue Thread Safe Queue pour les données normalisé des websockets
         * @param opt_input_queue Thread Safe Queue qui permet d'envoyer des paquets History au worker optimiser. Queue de communication entre le worker des modèles, et le worker HPC.
         * @param opt_output_queue Thread Safe Queue qui permet de recevoir des paquets opt_HawkesParam du worker optimiser. Queue de communication entre le worker des modèles, et le worker HPC.
         */
        Worker(
            WorkerParam* workerParam,
            ThreadSafeQueue<normalized_data>& inqueue,
            ThreadSafeQueue<History>& opt_input_queue,
            ThreadSafeQueue<opt_hawkesParams>& opt_output_queue,
            TelemetryManager& telemetry_manager
        );

        void str_new_params(std::vector<double> alpha,std::vector<double> beta,std::vector<double> mu );
        
        /**
         * Fonction principal de la class, envoie les paquets History au worker HPC pour l'optimisation des paramètres, recoit les nouveaux paramètres et les diffuses dans les modèles qu'il gère.
         */
        void run();
};

class HawkesOptimizer {
    protected:
        ThreadSafeQueue<History>& opt_input_queue; // Queue Cores Safe qui permet d'envoyer l'information au worker HPC
        ThreadSafeQueue<opt_hawkesParams>& opt_output_queue; // Queue Cores Safe qui permet d'énvoyer les nouveaux paramètres optimisés
        NelderMeadConfig optimizerConfig;
        int n_websockets;

    public:
        /**
         * @param opt_input_queue Thread Safe Queue qui permet d'envoyer des paquets History au worker optimiser. Queue de communication entre le worker des modèles, et le worker HPC.
         * @param opt_output_queue Thread Safe Queue qui permet de recevoir des paquets opt_HawkesParam du worker optimiser. Queue de communication entre le worker des modèles, et le worker HPC.
         * @param n_websockets Nombre de websockets actifs
         */
        HawkesOptimizer(
            ThreadSafeQueue<History>& opt_input_queue,
            ThreadSafeQueue<opt_hawkesParams>& opt_output_queue,
            int n_websockets
        );

        /**
         * Fonction principal de la class, exécuté depuis le thread. Cette méthode, ajoute les données recu dans le buffer, et calcul l'intensité de hawkes en temps réel grâce à un schéma de calcul FPGA.
         */
        void run();

};


#endif 