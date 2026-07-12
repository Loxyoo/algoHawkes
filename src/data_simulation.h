#ifndef DATA_SIMULATION_
#define DATA_SIMULATION_

#include <cstdio>
#include <cmath>
#include <string>
#include <iostream>

#include "tools.h"

// Types C++ dédiés à la simulation Ogata (préfixés Sim* pour éviter tout
// conflit avec les types C homonymes utilisés par le pipeline d'optimisation
// (voir calibration/hawkes_model/include/struct.h).
typedef struct {
    double time;
    int type; // ID du Websocket (0 à N_DIM-1)
} SimEvent;

typedef struct {
    std::vector<SimEvent> events;
    std::string symbol;
    int total_events;
    double T_max;
} SimHistory;

typedef struct {
    int D;              // Nombre de dimensions
    double* mu;         // Tableau 1D de taille D
    double* alpha;      // Matrice aplatie 1D de taille D*D
    double* beta;       // Matrice aplatie 1D de taille D*D (ou vecteur D si beta ne dépend que de la cible)
    double* branching_matrix;
    double T_max;
} SimOgataParams;

double sim_rand_uniform();
double sim_rand_exponential(double lambda);
void sim_init_history(SimHistory *h);
void sim_add_event(SimHistory *h, double time, int type);
void sim_init_params(SimOgataParams* p, int dim, double T);
void sim_free_params(SimOgataParams* p);

/**
 * @brief Simule un processus de Hawkes multivarié à noyau exponentiel via
 *        l'algorithme d'amincissement d'Ogata, avec récurrence O(1) pour l'intensité.
 *
 * L'état de Hawkes R[i][j] agrège la contribution du dim j sur l'intensité de i
 * et se met à jour par décroissance exponentielle (advance) + saut à chaque event
 * accepté. La complexité totale est O(N·D²), indépendante du nombre d'events passés.
 *
 * @param time_scale Facteur d'accélération temps simulé / temps réel.
 *                   1.0 = temps réel (nanosleep pour aligner sur wall-clock).
 *                   100.0 = 100× plus rapide (1 s wall-clock = 100 s de sim).
 *                   <=0 = aucune attente, débit maximal CPU.
 */
void real_time_multivariate_ogata_sim(ThreadSafeQueue<normalized_data>& shared_queue,
                                      SimOgataParams* op,
                                      SimHistory* result,
                                      double time_scale = 1.0);

#endif
