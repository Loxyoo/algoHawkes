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
#define N_WS 5

// Structure du vecteur pour UNE dimension cible : [Mu, Alpha_0..N, Beta_0..N]
#define N_PARAMS_PER_DIM (1 + 2 * N_WS) 

// Offsets définitifs
#define OFF_MU    0
#define OFF_ALPHA 1            
#define OFF_BETA  (1 + N_WS)   

// Structures de données
typedef struct {
    int n_dim; 
    int status;
    char* symbol;         
    double* alpha;      // Matrice D*D
    double* beta;       // Matrice D*D
    double* mu;         // Vecteur D
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
} NelderMeadConfig;

typedef struct {
    double* phi;
    double* lambda; // Pas strictement nécessaire ici, mais utile pour debug
    double last_t_global; // Essentiel pour le multivarié
} LL_params;

#endif