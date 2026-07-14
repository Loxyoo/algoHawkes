#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <time.h>
#include <string.h>
#include "../include/ogata_thinning_algo.h"
#include "../include/optimization.h"

// --- Main Généralisé ---
// NOTE : ce test standalone utilise l'ANCIENNE API multi-dimensionnelle de
// hawkes_model_optim (ModelParams avec mu/alpha/beta pleins). Depuis le passage
// à l'optimisation dimension-par-dimension, il n'est plus compatible et doit être
// réécrit (boucler sur target_dim). On le retire de la compilation par défaut pour
// ne pas casser le build de MonApp (ce .c est ramassé par le GLOB_RECURSE du CMake).
// Pour le rebâtir : compiler avec -DHAWKES_TEST_MAIN après mise à jour de l'appel.
#ifdef HAWKES_TEST_MAIN
int main_hawkes() {
    srand(time(NULL));

    // 1. Paramètres dynamiques
    int dimensions = N_DIM; // Peut être changé ou lu via argv
    double T = 50000.0;
    
    OgataParams params;
    init_params(&params, dimensions, T);

    // Affichage de la matrice d'interaction générée (Optionnel)
    printf("Matrice Alpha (Interaction) :\n");
    for(int i=0; i<dimensions; i++) {
        printf("[ ");
        for(int j=0; j<dimensions; j++) {
            printf("%.2f ", params.alpha[i*dimensions + j]);
        }
        printf("]\n");
    }
    printf("\n");

    // 2. Simulation
    History h;
    init_history(&h);
    h.T_max = T;

    printf("Lancement simulation pour %d dimensions sur T=%.1f...\n", dimensions, T);
    multivariate_ogata_sim(&params, &h);

    // 3. Résultats
    printf("Simulation terminee. Total events: %d\n", h.total_events);
    
    // Comptage par dimension
    int* counts = calloc(dimensions, sizeof(int));
    for(int i=0; i<h.total_events; i++) {
        counts[h.events[i].type]++;
    }

    printf("Repartition par dimension :\n");
    for(int i=0; i<dimensions; i++) {
        printf("  Dim %d : %d events (Intensite base mu=%.2f)\n", 
               i, counts[i], params.mu[i]);
    }

    // Estimation des paramètres grâce à history
    // --- AJOUT : ESTIMATION DES PARAMETRES ---
    printf("\n--- Debut de l'estimation des parametres (Nelder-Mead) ---\n");

    // 1. Configuration de l'optimiseur
    NelderMeadConfig conf;
    conf.max_iter = 5000; // Assez d'itérations pour converger
    conf.rho = 1.0;
    conf.chi = 2.0;
    conf.psi = 0.5;
    conf.sigma = 0.5;
    conf.n_dim = dimensions; // Dimension du processus (runtime)
    
    // Le nombre de paramètres PAR dimension est : 1 Mu + N_DIM Alpha + N_DIM Beta
    int n_params_local = 1 + 2 * dimensions; 
    conf.bounds = malloc(n_params_local * sizeof(param_bounds));

    // Définition des bornes de recherche (Bounds)
    // Offset 0 : Mu
    conf.bounds[0].min = 0.001; 
    conf.bounds[0].max = 2.0;   // Mu est rarement très élevé
    
    // Offsets 1 à N : Alpha (Interaction)
    for(int i=0; i<dimensions; i++) {
        conf.bounds[1 + i].min = 0.0; 
        conf.bounds[1 + i].max = 0.5; // Alpha peut monter si Beta monte
    }
    
    // Offsets 1+N à 1+2N : Beta (Decay)
    for(int i=0; i<dimensions; i++) {
        conf.bounds[1 + dimensions + i].min = 0.5; // Decay pas trop lent (mémoire infinie)
        conf.bounds[1 + dimensions + i].max = 10.0; // Decay pas instantané
    }

    // 2. Lancement de l'optimisation
    // Cette fonction va boucler sur chaque dimension et remplir la structure résultat
    ModelParams* estimated = hawkes_model_optim(&h, &conf);

    // 3. Affichage Comparatif (Vérité vs Estimation)
    printf("\n========================================\n");
    printf("   RESULTATS : VERITE vs ESTIMATION     \n");
    printf("========================================\n");
    
    for(int k=0; k<dimensions; k++) {
        printf("\n[TARGET DIM %d] (Base Intensity Mu: Vrai=%.3f | Est=%.3f)\n", 
               k, params.mu[k], estimated->mu[k]);
        printf("  Influences recues (Alpha) et vitesse d'oubli (Beta) :\n");
        printf("  -----------------------------------------------------\n");
        
        for(int src=0; src<dimensions; src++) {
            // Index dans la matrice aplatie
            int idx = k * dimensions + src;
            
            double a_true = params.alpha[idx];
            double a_est  = estimated->alpha[idx];
            double b_true = params.beta[idx];
            double b_est  = estimated->beta[idx];

            // On affiche si interaction réelle ou estimée significative
            if (a_true > 0.01 || a_est > 0.01) {
                printf("   <- Depuis Source %d : Alpha[V=%.3f, E=%.3f]  Beta[V=%.3f, E=%.3f]\n", 
                       src, a_true, a_est, b_true, b_est);
            }
        }
    }
    printf("========================================\n");

    // 4. Nettoyage mémoire spécifique à l'estimation
    free(conf.bounds);
    
    // Libération de la structure ModelParams retournée
    free(estimated->mu);
    free(estimated->alpha);
    free(estimated->beta);
    free(estimated);

    // 4. Nettoyage mémoire
    free(counts);
    free_history(&h);
    free_params(&params);

    return EXIT_SUCCESS;
}
#endif // HAWKES_TEST_MAIN