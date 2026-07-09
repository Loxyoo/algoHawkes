#include "data_simulation.h"

double sim_rand_uniform() {
    return (double)rand() / (double)RAND_MAX;
}

double sim_rand_exponential(double lambda) {
    if (lambda <= 1e-9) return 1e9; // Temps infini si lambda quasi nul
    double u = sim_rand_uniform();
    if (u > 0.999999999) u = 0.999999999;
    return -log(u) / lambda;
}

void sim_init_history(SimHistory *h) {
    h->total_events = 0;
}

void sim_add_event(SimHistory *h, double time, int type) {
    h->events.push_back({time, type});
    h->total_events++;
}

// Paramètres calibrés pour un régime haute fréquence (HFT-like) tout en restant
// dans un régime sous-critique (spectral radius de α/β < 1). Baseline typique
// ~30 Hz par dim, clustering fort via alpha dense.
void sim_init_params(SimOgataParams* p, int dim, double T) {
    p->D = dim;
    p->T_max = T;

    p->mu = new double[dim];
    p->alpha = new double[dim * dim];
    p->beta = new double[dim * dim];

    printf("Initialisation des parametres HF pour D=%d...\n", dim);

    for (int i = 0; i < dim; i++) {
        // Intensité de base : 10 à 50 Hz (au lieu de 0.1-0.5)
        p->mu[i] = 10.0 + sim_rand_uniform() * 40.0;

        for (int j = 0; j < dim; j++) {
            // Beta : décroissance rapide (échelle 100-500 ms)
            p->beta[i * dim + j] = 3.0 + sim_rand_uniform() * 5.0;

            // Alpha : 60% de connexion (au lieu de 30%), amplitude ×5
            if (sim_rand_uniform() > 0.4) {
                p->alpha[i * dim + j] = 0.5 + sim_rand_uniform() * 1.5;
            } else {
                p->alpha[i * dim + j] = 0.0;
            }
        }
    }
}

void sim_free_params(SimOgataParams* p) {
    delete[] p->mu;
    delete[] p->alpha;
    delete[] p->beta;
}

void real_time_multivariate_ogata_sim(ThreadSafeQueue<normalized_data>& shared_queue,
                                      SimOgataParams* op,
                                      SimHistory* result,
                                      double time_scale) {
    double s = 0;
    int D = op->D;

    // État Hawkes en récurrence exponentielle : R[i*D+j] agrège la contribution
    // du dim j sur l'intensité de i. λᵢ(t) = μᵢ + Σⱼ R[i,j](t).
    // R[i,j] décroît en exp(-βᵢⱼ·dt) et saute de αᵢⱼ quand un event de dim j survient.
    // Complexité : O(D²) par candidat, indépendante du nombre d'events.
    std::vector<double> R(D * D, 0.0);
    double t_last = 0.0;

    std::vector<double> lambda_sup_per_dim(D);
    std::vector<double> lambda_true(D);

    double lambda_sup_total = 0;
    for (int i = 0; i < D; i++) {
        lambda_sup_per_dim[i] = op->mu[i];
        lambda_sup_total += lambda_sup_per_dim[i];
    }

    struct timespec t0;
    clock_gettime(CLOCK_MONOTONIC, &t0);

    while (s < op->T_max) {
        if (lambda_sup_total <= 1e-10) lambda_sup_total = 1e-10;

        double w = sim_rand_exponential(lambda_sup_total);
        s += w;
        if (s >= op->T_max) break;

        // Advance R de t_last à s (décroissance exponentielle continue)
        double dt = s - t_last;
        for (int i = 0; i < D; i++) {
            for (int j = 0; j < D; j++) {
                int idx = i * D + j;
                R[idx] *= exp(-op->beta[idx] * dt);
            }
        }
        t_last = s;

        // Intensité vraie en s : λᵢ = μᵢ + Σⱼ R[i,j]
        double lambda_true_total = 0;
        for (int i = 0; i < D; i++) {
            double val = op->mu[i];
            for (int j = 0; j < D; j++) val += R[i * D + j];
            lambda_true[i] = val;
            lambda_true_total += val;
        }

        double u = sim_rand_uniform() * lambda_sup_total;

        if (u <= lambda_true_total) {
            int selected_dim = -1;
            double current_sum = 0;
            for (int k = 0; k < D; k++) {
                current_sum += lambda_true[k];
                if (u <= current_sum) { selected_dim = k; break; }
            }
            if (selected_dim == -1) selected_dim = D - 1;

            // Alignement wall-clock : n'attend que si time_scale > 0.
            // time_scale sim-seconds par wall-second : dépasser 1 accélère la simu.
            if (time_scale > 0.0) {
                struct timespec now;
                clock_gettime(CLOCK_MONOTONIC, &now);
                double elapsed = (now.tv_sec - t0.tv_sec)
                               + (now.tv_nsec - t0.tv_nsec) * 1e-9;
                double wait = (s / time_scale) - elapsed;
                if (wait > 0) {
                    struct timespec ts;
                    ts.tv_sec = (time_t)wait;
                    ts.tv_nsec = (long)((wait - (double)ts.tv_sec) * 1e9);
                    nanosleep(&ts, NULL);
                }
            }

            // Saut de R au moment d'un event de dim selected_dim :
            // Rᵢ,selected_dim += αᵢ,selected_dim pour tout i.
            for (int i = 0; i < D; i++) {
                int idx = i * D + selected_dim;
                R[idx] += op->alpha[idx];
            }

            sim_add_event(result, s, selected_dim);

            normalized_data data;
            data.timestamp = s;
            data.symbol = result->symbol;

            data.exchange = "exchange_" + std::to_string(selected_dim);
            shared_queue.push(data);

            // Nouvelle borne sup : λ_true + saut potentiel sur chaque dim.
            lambda_sup_total = 0;
            for (int i = 0; i < D; i++) {
                double jump = op->alpha[i * D + selected_dim];
                lambda_sup_per_dim[i] = lambda_true[i] + jump;
                lambda_sup_total += lambda_sup_per_dim[i];
            }
        } else {
            // Rejet : R déjà advancé jusqu'à s, la borne sup se resserre à λ_true.
            lambda_sup_total = lambda_true_total;
            for (int i = 0; i < D; i++) lambda_sup_per_dim[i] = lambda_true[i];
        }
    }
}

#ifdef OGATA_TEST_MAIN_CPP
int main(int argc, char** argv) {
    srand(time(NULL));

    int dimensions      = (argc > 1) ? atoi(argv[1]) : 3;
    double T            = (argc > 2) ? atof(argv[2]) : 60.0;
    double time_scale   = (argc > 3) ? atof(argv[3]) : 1.0;

    if (dimensions <= 0) {
        fprintf(stderr, "Dimension invalide : %d\n", dimensions);
        return EXIT_FAILURE;
    }

    SimOgataParams params;
    sim_init_params(&params, dimensions, T);

    SimHistory h;
    sim_init_history(&h);
    h.T_max = T;
    h.symbol = "test_asset";

    std::cout << "\n--- Simulation Ogata en temps reel (D=" << dimensions
              << ", T=" << T << " s, time_scale=" << time_scale << "x) ---\n";

    ThreadSafeQueue<normalized_data> shared_queue;

    struct timespec t_rt0, t_rt1;
    clock_gettime(CLOCK_MONOTONIC, &t_rt0);
    real_time_multivariate_ogata_sim(shared_queue, &params, &h, time_scale);
    clock_gettime(CLOCK_MONOTONIC, &t_rt1);
    double elapsed_rt = (t_rt1.tv_sec - t_rt0.tv_sec)
                      + (t_rt1.tv_nsec - t_rt0.tv_nsec) * 1e-9;

    std::cout << "Simulation terminee en " << elapsed_rt << " s wall-clock. "
              << "Total events : " << h.total_events << " ("
              << (h.total_events / T) << " Hz sim, "
              << (h.total_events / elapsed_rt) << " Hz wall)\n";

    sim_free_params(&params);

    return EXIT_SUCCESS;
}
#endif
