#ifndef OGATA_THINNING_ALGO_H_
#define OGATA_THINNING_ALGO_H_

#include "../include/struct.h"

// --- Helpers Mathématiques ---
double rand_uniform();
double rand_exponential(double lambda);

// --- Gestion Mémoire Historique ---
void init_history(History* h);
void add_event(History* h, double time, int type);
void free_history(History* h);
void init_params(OgataParams* p, int dim, double T);
void free_params(OgataParams* p);

// Calculs
double multi_intensity(double u, History* h, int target_dim, OgataParams* op);

// --- Simulation Ogata Généralisée ---
void multivariate_ogata_sim(OgataParams* op, History* result);

#endif