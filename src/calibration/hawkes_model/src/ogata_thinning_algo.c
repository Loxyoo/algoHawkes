#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <time.h>
#include <string.h>
#include "../include/ogata_thinning_algo.h"


// --- Helpers Mathématiques ---

double rand_uniform() {
    return (double)rand() / (double)RAND_MAX;
}

double rand_exponential(double lambda) {
    if (lambda <= 1e-9) return 1e9; // Temps infini si lambda quasi nul
    double u = rand_uniform();
    if (u > 0.999999999) u = 0.999999999; 
    return -log(u) / lambda;
}

// --- Gestion Mémoire Historique ---

void init_history(History* h) {
    h->capacity = 1000; 
    h->total_events = 0;
    h->events = malloc(h->capacity * sizeof(Event));
}

void add_event(History* h, double time, int type) {
    if (h->total_events >= h->capacity) {
        h->capacity *= 2;
        h->events = realloc(h->events, h->capacity * sizeof(Event));
    }
    h->events[h->total_events].time = time;
    h->events[h->total_events].type = type;
    h->total_events++;
}

void free_history(History* h) {
    free(h->events);
}

// --- Gestion Mémoire Paramètres (Généralisation) ---

void init_params(OgataParams* p, int dim, double T) {
    p->D = dim;
    p->T_max = T;
    
    // Allocation dynamique
    p->mu = malloc(dim * sizeof(double));
    p->alpha = malloc(dim * dim * sizeof(double));
    p->beta = malloc(dim * dim * sizeof(double));

    // Génération aléatoire de paramètres STABLES
    // Pour la stabilité, il faut idéalement que le rayon spectral de Gamma (Alpha/Beta) soit < 1.
    // Ici, on fait simple : des petits alpha et des grands beta.
    
    printf("Initialisation des parametres pour D=%d...\n", dim);
    
    for (int i = 0; i < dim; i++) {
        // Intensité de base aléatoire entre 0.1 et 0.5
        p->mu[i] = 0.1 + rand_uniform() * 0.4;
        
        for (int j = 0; j < dim; j++) {
            // Beta (décroissance) rapide (entre 2.0 et 5.0)
            p->beta[i * dim + j] = 2.0 + rand_uniform() * 3.0;

            // Alpha (excitation)
            // On veut une matrice creuse (tous ne sont pas connectés)
            if (rand_uniform() > 0.7) { 
                // 30% de chance d'avoir une connexion
                // Alpha petit pour éviter l'explosion (0.1 à 0.5)
                p->alpha[i * dim + j] = 0.1 + rand_uniform() * 0.4;
            } else {
                p->alpha[i * dim + j] = 0.0;
            }
        }
    }
}

void free_params(OgataParams* p) {
    free(p->mu);
    free(p->alpha);
    free(p->beta);
}

// --- Cœur du Calcul ---

double multi_intensity(double u, History* h, int target_dim, OgataParams* op) {
    double val = op->mu[target_dim];
    int D = op->D;

    for (int i = 0; i < h->total_events; i++) {
        Event e = h->events[i];
        if (e.time >= u) break; 

        int source_dim = e.type;
        double dt = u - e.time;

        // Accès matrice aplatie : alpha[Ligne * Largeur + Colonne]
        // alpha[target][source]
        int idx = target_dim * D + source_dim;
        
        double a_val = op->alpha[idx];
        
        if (a_val > 0) {
            val += a_val * exp(-op->beta[idx] * dt);
        }
    }
    return val;
}

// --- Simulation Ogata Généralisée ---

void multivariate_ogata_sim(OgataParams* op, History* result) {
    double s = 0;
    int D = op->D;
    
    // Allocations temporaires
    double* lambda_sup_per_dim = malloc(D * sizeof(double));
    double* lambda_true = malloc(D * sizeof(double));

    // Init bornes sup avec Mu
    double lambda_sup_total = 0;
    for(int i=0; i<D; i++) {
        lambda_sup_per_dim[i] = op->mu[i];
        lambda_sup_total += lambda_sup_per_dim[i];
    }

    while (s < op->T_max) {
        if (lambda_sup_total <= 1e-10) lambda_sup_total = 1e-10;

        // 1. Temps global suivant
        double w = rand_exponential(lambda_sup_total);
        s += w;
        if (s >= op->T_max) break;

        // 2. Calcul intensités réelles
        double lambda_true_total = 0;
        for (int i = 0; i < D; i++) {
            lambda_true[i] = multi_intensity(s, result, i, op);
            lambda_true_total += lambda_true[i];
        }

        // 3. Test de rejet
        double u = rand_uniform() * lambda_sup_total;

        if (u <= lambda_true_total) {
            // ACCEPTÉ : Sélection dimension
            int selected_dim = -1;
            double current_sum = 0;
            
            for (int k = 0; k < D; k++) {
                current_sum += lambda_true[k];
                if (u <= current_sum) {
                    selected_dim = k;
                    break;
                }
            }
            if (selected_dim == -1) selected_dim = D - 1;

            add_event(result, s, selected_dim);

            // 4. Mise à jour bornes sup
            lambda_sup_total = 0;
            for (int i = 0; i < D; i++) {
                // alpha[i][selected_dim] -> i * D + selected_dim
                double jump = op->alpha[i * D + selected_dim];
                lambda_sup_per_dim[i] = lambda_true[i] + jump;
                lambda_sup_total += lambda_sup_per_dim[i];
            }
        } else {
            // REJETÉ
            lambda_sup_total = lambda_true_total;
            for (int i = 0; i < D; i++) {
                lambda_sup_per_dim[i] = lambda_true[i];
            }
        }
    }

    free(lambda_sup_per_dim);
    free(lambda_true);
}
