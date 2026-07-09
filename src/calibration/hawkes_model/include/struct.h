#ifndef STRUCT_H_
#define STRUCT_H_

// On définit N ici, mais le code est conçu pour fonctionner 
// même si cette valeur change ou est passée en argument.
#define N_DIM 5

// --- Structures ---
typedef struct {
    double time;
    int type; // ID du Websocket (0 à N_DIM-1)
} Event;

typedef struct {
    Event* events;
    char* symbol;
    int capacity;
    int total_events;
    double T_max;
} History;

typedef struct {
    int D;              // Nombre de dimensions
    double* mu;         // Tableau 1D de taille D
    double* alpha;      // Matrice aplatie 1D de taille D*D
    double* beta;       // Matrice aplatie 1D de taille D*D (ou vecteur D si beta ne dépend que de la cible)
    double T_max;
} OgataParams;

// --- 1. DEFINITIONS PROPRES ---
// Valeur par défaut historique (5 exchanges). Le pipeline d'optimisation n'utilise
// PLUS cette constante : la dimension réelle est passée à l'exécution via
// NelderMeadConfig.n_dim (voir optimization.c). On la garde uniquement comme
// valeur par défaut pour le code de test standalone.
#define N_WS 5

// Nombre de paramètres pour UNE dimension cible : [Mu, Alpha_0..n_dim-1, Beta_0..n_dim-1].
// Se calcule à l'exécution avec 1 + 2 * n_dim.
#define N_PARAMS_FOR_DIM(n_dim) (1 + 2 * (n_dim))

// Offsets fixes. OFF_BETA dépend de la dimension et vaut (1 + n_dim) à l'exécution.
#define OFF_MU    0
#define OFF_ALPHA 1

// Structures de données
typedef struct {
    int n_dim; 
    int status;
    char* symbol;         
    double* alpha;      // Matrice D*D
    double* beta;       // Matrice D*D
    double* mu;         // Vecteur D
    double* phi;        // Vecteur D ajusté pour le calcul de l'intensité et du compensateur
} ModelParams;

typedef struct {
    double min;
    double max;
} param_bounds;

typedef struct {
    double* values;     
    double score;       
} SimplexPoint;

typedef struct {
    int n_dim_params;       
    SimplexPoint** vertices; 
} Simplex;

typedef struct {
    int max_iter;
    double rho; double chi; double psi; double sigma;
    param_bounds* bounds;
    int n_dim; // Nombre de dimensions du processus (runtime, remplace N_WS)
} NelderMeadConfig;

typedef struct {
    int n_dim;    // Nombre de dimensions (taille des buffers phi/lambda)
    double* phi;
    double* lambda; // Pas strictement nécessaire ici, mais utile pour debug
    double last_t_global; // Essentiel pour le multivarié
} LL_params;

#endif