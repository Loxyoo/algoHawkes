#include <stdlib.h>
#include <stdio.h>
#include <math.h>
#include <float.h>
#include <assert.h>
#include <string.h> // Pour memcpy
#include "../include/optimization.h"




// --- 2. GESTION MEMOIRE & INIT ---

SimplexPoint* init_simplexpoint(int n_params) {
    SimplexPoint* p = malloc(sizeof(SimplexPoint));
    p->values = calloc(n_params, sizeof(double));
    p->score = 0.0;
    return p;
}

void free_simplexpoint(SimplexPoint* p) {
    if (p) { free(p->values); free(p); }
}

Simplex* init_simplex(int n_params) {
    Simplex* s = malloc(sizeof(Simplex));
    s->n_dim_params = n_params;
    s->vertices = malloc((n_params + 1) * sizeof(SimplexPoint*));
    for(int i = 0; i < n_params + 1; i++) s->vertices[i] = init_simplexpoint(n_params);
    return s;
}

void free_simplex(Simplex* s, int n_params) {
    if (s) {
        for(int i=0; i<=n_params; i++) free_simplexpoint(s->vertices[i]);
        free(s->vertices);
        free(s);
    }
}

LL_params* init_ll_params() {
    LL_params* p = malloc(sizeof(LL_params));
    p->phi = calloc(N_WS, sizeof(double));
    p->lambda = calloc(N_WS, sizeof(double));
    p->last_t_global = -1;
    return p;
}

void free_ll_params(LL_params* p) {
    if(p) { free(p->phi); free(p->lambda); free(p); }
}

void reset_ll_params(LL_params* p) {
    for(int i=0; i<N_WS; i++) p->phi[i] = 0.0;
    p->last_t_global = -1;
}

// --- 3. GENERATION POINTS (CORRIGÉE AVEC OFFSETS) ---

Simplex* init_points(param_bounds* bounds, int n_params) {
    Simplex* simplex = init_simplex(n_params);
    
    for(int k = 0; k < n_params + 1; k++) {
        // A. Génération aléatoire brute
        for(int i = 0; i < n_params; i++) {
            double delta = bounds[i].max - bounds[i].min;
            simplex->vertices[k]->values[i] = bounds[i].min + ((double)rand()/RAND_MAX) * delta;
        }

        // B. Application des contraintes Hawkes (Alpha < Beta)
        // On itère sur les sources (0 à N_WS-1)
        for(int src = 0; src < N_WS; src++) {
            // CORRECTION ICI : Utilisation des bons OFFSETS
            double alpha = simplex->vertices[k]->values[OFF_ALPHA + src];
            double beta  = simplex->vertices[k]->values[OFF_BETA + src];

            if (alpha < 0) alpha = 0.001; // Force positif
            if (beta <= 0.01) beta = 0.1; 

            // Stabilité : Alpha doit être < Beta (Spectral radius local)
            if (alpha >= beta) {
                alpha = beta * 0.5; // On réduit alpha
            }

            simplex->vertices[k]->values[OFF_ALPHA + src] = alpha;
            simplex->vertices[k]->values[OFF_BETA + src]  = beta;
        }
        
        // Contrainte Mu positif
        if(simplex->vertices[k]->values[OFF_MU] < 0) simplex->vertices[k]->values[OFF_MU] = 0.01;
    }
    return simplex;
}

// --- 4. FONCTION OBJECTIVE (LOG VRAISEMBLANCE) ---

double calculate_ll(SimplexPoint* p, History* history, LL_params* ll_params, int target_dim) {
    double T_max = history->T_max;
    double mu = p->values[OFF_MU];

    reset_ll_params(ll_params);
    
    double log_sum = 0.0;
    int i;
    for(i = 0; i < history->total_events; i++) {
        double t_now = history->events[i].time;
        int type_src = history->events[i].type;

        if (type_src < 0 || type_src >= N_WS) {
            printf("Warning: Invalid source type %d at event %d/%d from calculate_ll function\n", type_src, i, history->total_events);
            continue; // Invalide la configuration
        }

        // 1. Decay global
        double dt = t_now - ll_params->last_t_global;
        if (ll_params->last_t_global == -1) dt = 0;

        for(int src=0; src < N_WS; src++) {
             double b = p->values[OFF_BETA + src];
             ll_params->phi[src] *= exp(-b * dt);
        }
        
        // 2. Calcul Intensité (si c'est la cible)
        if (type_src == target_dim) {
            double lambda = mu;
            for(int src=0; src < N_WS; src++) {
                double a = p->values[OFF_ALPHA + src];
                lambda += a * ll_params->phi[src];
            }
            if(lambda <= 1e-9) lambda = 1e-9;
            log_sum += log(lambda);
        }

        // 3. Jump
        // printf("s:%d,it:%d\ntl:%d", type_src, i, history->total_events);
        ll_params->phi[type_src] += 1.0;
        ll_params->last_t_global = t_now;
    }

    // 4. Intégrale
    double integral = mu * T_max;
    for(int i=0; i < history->total_events; i++) {
        int src = history->events[i].type;
        double a = p->values[OFF_ALPHA + src];
        double b = p->values[OFF_BETA + src];
        integral += (a / b) * (1.0 - exp(-b * (T_max - history->events[i].time)));
        //printf("a:%lf b:%lf \n", a, b);
    }

    return log_sum - integral; // On retourne LL (Maximisation)
}

// Wrapper avec Barrière et Pénalités
void ll_objective(SimplexPoint* p, History* history, LL_params* ll_params, int target_dim) {
    // 1. Check contraintes Hard
    int invalid = 0;
    if (p->values[OFF_MU] < 0) invalid = 1;
    
    for(int src=0; src<N_WS; src++) {
        double a = p->values[OFF_ALPHA + src];
        double b = p->values[OFF_BETA + src];
        if (a < 0 || b <= 0 || a >= b) invalid = 1;
    }

    if (invalid) {
        p->score = DBL_MAX;
        return;
    }

    // 2. Calcul LL
    double ll = calculate_ll(p, history, ll_params, target_dim);
    
    // 3. Transformation en coût (Minimisation)
    p->score = -ll;

    // 4. Barrière Logarithmique Soft
    double barrier_weight = 100.0;
    for(int src=0; src<N_WS; src++) {
        double a = p->values[OFF_ALPHA + src];
        double b = p->values[OFF_BETA + src];
        // On pénalise si on s'approche trop de alpha = beta
        p->score -= barrier_weight * log(b - a); 
    }
}

// --- 5. NELDER MEAD ---

// Helpers géométriques (Barycentre, etc.) omis pour brièveté, garde les tiens mais...
// ATTENTION: update_simplexPoint doit copier tout le tableau values !
void update_simplexPoint(SimplexPoint* dest, SimplexPoint* src, int n_params) {
    dest->score = src->score;
    memcpy(dest->values, src->values, n_params * sizeof(double));
}

// calcul du barycentre sur tout les points sauf la pire
SimplexPoint* barycentre(Simplex* simplex, int n_params) {
    SimplexPoint* p = init_simplexpoint(n_params);
    for (int i = 0; i < n_params; i++) {
        for (int j = 0; j < n_params; j++) {
            p->values[j] += simplex->vertices[i]->values[j];
        }
    }
    for (int i = 0; i < n_params; i++) {
        // On divise par n_params pour obtenir une moyenne et donc le barycentre
        p->values[i] /= n_params;
    }

    return p;
}

// Homothétie de rapport sigma et de centre simplex->vertices[0]
void homothetie(Simplex* simplex, int n_params, double sigma) {
    for (int i = 1; i < n_params; i++) {
        for (int j = 0; j < n_params; j++) {
            double x = simplex->vertices[0]->values[j];
            double y = simplex->vertices[i]->values[j];
            simplex->vertices[i]->values[j] = x + sigma * (y - x);
        }
    }
}


double mReflexion(double p1, double p2, double coeff) {return p1 + coeff * (p1 - p2);} 
double mExpansion(double p1, double p2, double coeff) {return p1 + coeff * (p2 - p1);}
double mContraction(double p1, double p2, double coeff) {return p1 + coeff * (p2 - p1);}
// Fonction appliquant une transformation géométrique. La méthode est définite par la fonction de type fmethod
// qui peut être reflexion, contraction, extansion, ...
SimplexPoint* transformation(SimplexPoint* p1, SimplexPoint* p2, fmethod fm, int n_params, double coeff) {
    SimplexPoint *transfo = init_simplexpoint(n_params);
    for (int j = 0; j < n_params; j++) {
        transfo->values[j] = fm(p1->values[j], p2->values[j], coeff);
    }
    return transfo;
}

// Fonction de tri
int compare_score(const void* a, const void* b) {
    // Attention: qsort passe des pointeurs vers les éléments du tableau
    // Ici le tableau est un tableau de pointeurs (SimplexPoint**)
    SimplexPoint* pA = *(SimplexPoint**)a;
    SimplexPoint* pB = *(SimplexPoint**)b;
    if (pA->score < pB->score) return -1;
    if (pA->score > pB->score) return 1;
    return 0;
}


SimplexPoint* nelder_mead_optim(
        History *history,
        NelderMeadConfig conf,
        LL_params *ll_params, // On reçoit le pointeur, on ne l'alloue pas !
        int target_dim) {
            
    int n_params = N_PARAMS_PER_DIM;
    
    // Init points avec contraintes corrigées
    Simplex* simplex = init_points(conf.bounds, n_params);
    
    // Eval initiale
    for (int i = 0; i <= n_params; i++) {
        ll_objective(simplex->vertices[i], history, ll_params, target_dim);
    }

    // Boucle principale (version simplifiée pour l'exemple)
    for (int iter = 0; iter < conf.max_iter; iter++) {
        // Tri (Note: ta fonction compare attend SimplexPoint*, mais qsort sur un tableau de pointeurs envoie SimplexPoint**)
        // J'ai corrigé compare_score plus haut.
        qsort(simplex->vertices, n_params + 1, sizeof(SimplexPoint*), compare_score);

        // Extraction des points clés
        SimplexPoint* last_p = simplex->vertices[n_params]; // pire score
        SimplexPoint* pointN = simplex->vertices[n_params-1]; // deuxième pire score
        SimplexPoint* centroide = barycentre(simplex, n_params);
        // Calcul du score du centroide
        // Son score peut être soumis à la barrière
        ll_objective(centroide, history, ll_params, target_dim);
    
        // Calcul da la reflexion
        SimplexPoint* reflexion = transformation(centroide, last_p, mReflexion, n_params, conf.rho);
        // Calcul du score de la reflexion, toujours avec la barrière
        ll_objective(reflexion, history, ll_params, target_dim);
    
        // Arbre de décision
        if (compare_score(simplex->vertices[0], reflexion) <= 0 && compare_score(reflexion, pointN) <= 0) {
            update_simplexPoint(last_p, reflexion, n_params);
        } else if (compare_score(reflexion, simplex->vertices[0]) < 0) {
            // La reflexion est meilleur que le meilleur point : on essaye l'expansion du point
            SimplexPoint* expansion = transformation(centroide, reflexion, mExpansion, n_params, conf.chi);
            // Calcul du score expansion avec la barrière
            ll_objective(expansion, history, ll_params, target_dim);
    
            if (compare_score(expansion, reflexion) <= 0) {
                update_simplexPoint(last_p, expansion, n_params);
            } else {
                update_simplexPoint(last_p, reflexion, n_params);
            }
            free_simplexpoint(expansion);
        } else if (compare_score(reflexion, pointN) >= 0) {
            // La reflexion est pire que le deuxième pire point : essaye la contraction
            SimplexPoint* contraction = transformation(centroide, last_p, mContraction, n_params, conf.psi);
            // Calcul du score contraction avec la barrière
            ll_objective(contraction, history, ll_params, target_dim);
    
            if (compare_score(contraction, last_p) < 0) {
                update_simplexPoint(last_p, contraction, n_params);
            } else {
                homothetie(simplex, n_params, conf.sigma);
            }
            free_simplexpoint(contraction);
        } else {
            homothetie(simplex, n_params, conf.sigma);
        }
        free_simplexpoint(reflexion);
        free_simplexpoint(centroide);
    }

    // Meilleur point
    SimplexPoint* best = init_simplexpoint(n_params);
    update_simplexPoint(best, simplex->vertices[0], n_params);
    
    free_simplex(simplex, n_params);
    return best;
}

// --- 6. FONCTION PRINCIPALE ---

static void display_vector(double* vec, int size, char* desc) {
    printf("%s \n", desc);
    for (int i = 0; i < size; i++) {
        printf("%lf ", vec[i]);
    }
    printf("\n");
}

ModelParams* hawkes_model_optim(History* history, NelderMeadConfig* conf) {
    // printf("Affichage des events. \n");
    // for (int i = 0; i < history->total_events; i++) {
    //     printf("(%lf,%d)", history->events[i].time, history->events[i].type);
    // }
    // printf("\n (Tmax %lf, t:%d) \n", history->T_max, history->total_events);
    // Allocations Globales

    // Définition des bornes pour chaque paramètre (Mu, Alpha_i, Beta_i)
    int n_params_local = 1 + 2 * N_WS;
    conf->bounds = malloc(n_params_local * sizeof(param_bounds));
    conf->bounds[0].min = 0.01; conf->bounds[0].max = 2.0; // Mu
    for(int i=1; i<n_params_local; i++) { // Alpha & Beta
        conf->bounds[i].min = 0.0; conf->bounds[i].max = 10.0;
    }

    ModelParams* global_model = malloc(sizeof(ModelParams));
    global_model->n_dim = N_WS;
    global_model->mu = malloc(N_WS * sizeof(double));
    global_model->alpha = malloc(N_WS * N_WS * sizeof(double)); // Matrice aplatie
    global_model->beta  = malloc(N_WS * N_WS * sizeof(double)); // Matrice aplatie

    // Buffer réutilisable pour éviter malloc dans la boucle
    LL_params* ll_params = init_ll_params(); 

    // Boucle d'Optimisation (Dimension par Dimension)
    for (int k = 0; k < N_WS; k++) {
        printf("Optimisation Dimension %d / %d ...\n", k+1, N_WS);
        
        // Lancement Optim
        SimplexPoint* res = nelder_mead_optim(history, *conf, ll_params, k);

        // Sauvegarde dans la structure globale
        // 1. Mu
        global_model->mu[k] = res->values[OFF_MU];
        
        // 2. Alpha et Beta (Lignes de la matrice)
        for(int src = 0; src < N_WS; src++) {
            // L'index dans la matrice globale est : ligne(k) * largeur + colonne(src)
            int global_idx = k * N_WS + src;
            
            global_model->alpha[global_idx] = res->values[OFF_ALPHA + src];
            global_model->beta[global_idx]  = res->values[OFF_BETA + src];
        }

        free_simplexpoint(res);
    }

    // // Affichage des nouveaux paramètres
    // printf("[C] : Paramètres \n");
    // display_vector(global_model->alpha, N_DIM*N_DIM, "ALPHA VALUES");
    // display_vector(global_model->beta, N_DIM*N_DIM, "BETA VALUES");
    // display_vector(global_model->mu, N_DIM, "MU VALUES");
    
    free_ll_params(ll_params);
    return global_model;
}

// Wrapper simplifié pour l'appel depuis Python
// Python envoie des tableaux bruts (times, types) et reçoit les paramètres optimisés dans un tableau de sortie.
void python_entry_point(
    double* times,      // Tableau des temps (venant de Python)
    int* types,         // Tableau des types (venant de Python)
    int count,          // Nombre d'événements
    double T_max,       // Temps max
    int n_ws,           // Nombre de dimensions
    double* out_mu,     // Buffer de sortie pour Mu (taille N_WS)
    double* out_alpha,  // Buffer de sortie pour Alpha (taille N_WS*N_WS)
    double* out_beta    // Buffer de sortie pour Beta (taille N_WS*N_WS)
) {
    // 1. Reconstruire la structure History attendue par ton code C
    History h;
    h.total_events = count;
    h.T_max = T_max;
    // On peut pointer directement sans copier si on fait attention, 
    // mais pour respecter ta struct Event*, on va allouer temporairement.
    // NOTE : Pour la performance pure, modifie ta struct History pour prendre double* et int* séparés.
    // Ici, on fait une conversion rapide :
    h.events = malloc(count * sizeof(Event));
    for(int i=0; i<count; i++) {
        h.events[i].time = times[i];
        h.events[i].type = types[i];
    }

    // 2. Configurer Nelder-Mead (Valeurs par défaut)
    NelderMeadConfig conf;
    conf.max_iter = 2000;
    conf.rho = 1.0; conf.chi = 2.0; conf.psi = 0.5; conf.sigma = 0.5;

    // 3. Lancer l'optimisation
    ModelParams* res = hawkes_model_optim(&h, &conf);

    // 4. Copier les résultats vers les buffers Python
    // C'est ici qu'on renvoie les données à Python
    for(int i=0; i<n_ws; i++) {
        out_mu[i] = res->mu[i];
    }
    for(int i=0; i<n_ws*n_ws; i++) {
        out_alpha[i] = res->alpha[i];
        out_beta[i]  = res->beta[i];
    }

    // 5. Nettoyage
    free(h.events);
    free(conf.bounds);
    free(res->mu); free(res->alpha); free(res->beta); free(res);
}