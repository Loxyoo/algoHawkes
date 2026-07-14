#ifndef OPTIMIZATION_H_
#define OPTIMIZATION_H_

#include "../include/struct.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief
 * 
 * @param n_params
 * 
 * @return SimplexPoint* 
 */
SimplexPoint* init_simplexpoint(int n_params);

/**
 * @brief Libère la mémoire allouée pour un SimplexPoint
 * 
 * @param p 
 */
void free_simplexpoint(SimplexPoint* p);

/**
 * @brief
 * 
 * @param n_params
 * 
 * @return Simplex*
 */
Simplex* init_simplex(int n_params);

/**
 * @brief
 * 
 * @param s 
 * @param n_params
 */
void free_simplex(Simplex* s, int n_params);

LL_params* init_ll_params(int n_dim);


void free_ll_params(LL_params* p);
void reset_ll_params(LL_params* p);

void homothetie(Simplex* simplex, int n_params, double sigma);
SimplexPoint* barycentre(Simplex* simplex, int n_params);
typedef double (*fmethod)(double p1, double p2, double coeff);
double mReflexion(double p1, double p2, double coeff);
double mExpansion(double p1, double p2, double coeff);
double mContraction(double p1, double p2, double coeff);
SimplexPoint* transformation(SimplexPoint* p1, SimplexPoint* p2, fmethod fm, int n_params, double coeff);

Simplex* init_points(param_bounds* bounds, int n_params);
double calculate_ll(SimplexPoint* p, History* history, LL_params* ll_params, int target_dim);
void ll_objective(SimplexPoint* p, History* history, LL_params* ll_params, int target_dim);
void update_simplexPoint(SimplexPoint* dest, SimplexPoint* src, int n_params);
int compare_score(const void* a, const void* b);
SimplexPoint* nelder_mead_optim(
        History *history,
        NelderMeadConfig conf,
        LL_params *ll_params, // On reçoit le pointeur, on ne l'alloue pas !
        int target_dim);
ModelParams* hawkes_model_optim(History* history, NelderMeadConfig* conf, int target_dim);

#ifdef __cplusplus
}
#endif

#endif // OPTIMIZATION_H_