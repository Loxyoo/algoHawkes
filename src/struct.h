#ifndef STRUCT_H
#define STRUCT_H

#include <string>
#include "tools.h"

typedef struct {
    std::string exchange;        // Binance, Coinbase, Kraken, etc.
    std::string symbol;          // BTCUSD, ETHUSD, etc.
    double price;           // Price of the asset
    double quantity;        // volume traded
    double timestamp;       // Epoch time of the trade
    double arrival_time;    // Time when the message was received
} normalized_data;

typedef struct{
    int calibration_time; // Calibrate duration
    int training_time; // Training duration
    int n_max_workers; // Number max of cores can be used
    std::vector<std::string> symbols; // List symbols
    // std::vector<std::unique_ptr<GenericWebSocket>> clients; // List of active clients
    Json::Value websocket_map; // Map of websockets associated to an integer ID
    Json::Value worker_mapping;
     // Input queue for normalized data from WebSocket connections
     Json::Value symbols_map; // Associate a symbol to an integer ID
} SchedulerConfig;

// Used to stock the number of data received from a symbol. Notably used in the core_packing function and the calibration phase
typedef struct {
    std::string symbol; // asset
    int volume; // Number of data received from symbol
} volume_symbol;

typedef enum {
    INITIALIZATION,
    CALIBRATING,
    TRAINING,
    WARNING,
    STOPPING
} SchedulerState;

typedef struct {
    std::string symbol;
    std::vector<double> intensities;
    double timestamp;
} intensities_data;

typedef struct {
    std::vector<double> intensities;
    std::string symbol;
    double timestamp;
} Snapshot;

/**
 * Cette structure est fourni aux paramètres d'initialisation d'un workers.
 */
typedef struct {
    Json::Value websocket_map; // Mapping des websockets à un integer/id
    std::vector<std::string> assets; // Liste des assets gérés par ce worker
    int n_websockets; // Nombre de websockets actif
    int training_duration; // Durée de l'entrainement des modèles
    int worker_id; // identifiant interger du worker
    Json::Value symbols_map; // Map qui associe un symbol à un integer/id
} WorkerParam;

typedef struct {
    normalized_data buffer; // Buffer qui va stocker les données associés au symbol
    int type; // Indice de l'exchange source
    std::string symbol; // Nom du symbol
} data_buffer;

/**
 * Enumération qui permet d'indiquer l'état d'un algorithme
 */
typedef enum {
    OK, // Tout va bien
    ERROR, // Erreur critique qui empêche de continuer
    WARN // Alerte, problème d'optimisation ou risque majeur
} AlgoState;

/**
 * Cette structure est celle qui sera généré avec tout les nouveaux paramètres optimisés.
 * Elle est généré dans le worker HPC, puis envoyé aux différents worker via une queue Safe Cores.
 */
typedef struct {
    std::string symbol; // Nom du symbol
    AlgoState status; // Status de l'algorithme d'optimisation
    std::vector<double> alpha; // Les paramètres de forces
    std::vector<double> beta; // Les paramètres d'oublis
    std::vector<double> mu; // Les intensités de fond
    std::vector<double> phi; // Nouveau phi ajuster avec les nouveaux paramètres pour calculer l'intensité et le compensateur de Hawkes.
} opt_hawkesParams;

#endif // STRUCT_H