#pragma once
#include <queue>
#include <mutex>
#include <shared_mutex>
#include <condition_variable>
#include <iostream>
#include <thread>
#include <vector>

#include "struct.h"

#ifdef __APPLE__
    #include <pthread.h>
    #include <sys/qos.h>
#else
    #include <pthread.h>
    #include <sched.h>
#endif

// Sur macOS, "core_id" est traité comme un "Tag de groupe", pas un index CPU physque.
inline void setCurrentThreadPriority() {
#ifdef __APPLE__
    // Sur M4, ceci est la seule façon fiable de dire "Je veux des P-Cores"
    pthread_set_qos_class_self_np(QOS_CLASS_USER_INITIATED, 0);
#else
    //
    //
#endif
}

inline double now_unix() {
    // Utilisation de system_clock pour générer un timestamp aligné sur les timestamps UNIX (données)
    // Renvoie le nombre de secondes écoulées depuis le 1er janvier 1970 (UNIX epoch) en tant que double
    using namespace std::chrono;
    return duration<double>(system_clock::now().time_since_epoch()).count();
}

template <typename T>
class ThreadSafeQueue {
private:
    std::queue<T> queue_;
    std::mutex mutex_;
    std::condition_variable cond_;

public:
    virtual ~ThreadSafeQueue() = default;

    /**
     * @brief push the value into the safe thread queue. 
     * 
     * @param value The value to push into the queue
     */
    void push(T value) {
        std::scoped_lock<std::mutex> lock(mutex_);
        queue_.push(value);
        cond_.notify_one(); // Réveille le consommateur
    }

    /**
     * @brief Renvoie la valeur en tête de la queue et l'enlève de la queue.
     */
    T pop() {
        std::unique_lock<std::mutex> lock(mutex_);
        // Attend qu'il y ait de la donnée (évite le while(true) qui brûle le CPU)
        cond_.wait(lock, [this]{ return !queue_.empty(); });
        T value = queue_.front();
        queue_.pop();
        return value;
    }
    
    /**
     * @brief Renvoie la valeur en tête de la queue sans l'enlever.
     */
    T get() {
        std::unique_lock<std::mutex> lock(mutex_);
        // Attend qu'il y ait de la donnée (évite le while(true) qui brûle le CPU)
        cond_.wait(lock, [this]{ return !queue_.empty(); });
        T value = queue_.front();
        return value;
    }

    /**
     * @brief Essaye d'enlever la valeur en tête de la queue. Si la queue est vide, il abandonne, et renvoie False, sinon True quand il réussi.
     * 
     * Méthode non-bloquante pour enlever une valeur. 
     */
    bool try_pop(T& value) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (queue_.empty()) {
            return false;
        }
        value = std::move(queue_.front());
        queue_.pop();
        return true;
    }

};

/**
 * @brief TelemetryManager est une classe qui gère les données de télémétrie partagées entre les workers et l'interface utilisateur.
 */
class TelemetryManager {
    public:
        std::vector<bool> symbols_to_look_at;

        struct Snapshot {
            std::vector<double> intensities;
            std::string symbol;
        };

        struct ResidualsSnapshot {
            std::vector<std::vector<double>> residuals_by_source; // un vecteur par source (source_idx)
            std::vector<std::vector<double>> ewma_by_source; // série temporelle de l'EWMA des résidus, par source (alignée sur residuals_by_source)
            std::string symbol;
        };

        struct ParametersSnapshot {
            opt_hawkesParams params;
            std::vector<double> branching_matrix;
        };

    private:
        std::vector<Snapshot> live_data;
        std::vector<ResidualsSnapshot> residuals_data;
        std::vector<std::string> symbols;
        std::vector<std::unique_ptr<std::shared_mutex>> mutexes;
        std::vector<ParametersSnapshot> parameters_snapshots; // Snapshot par symbole

    public:
        TelemetryManager(const std::vector<std::string>& syms) : symbols(syms) {
            size_t n = syms.size();
            live_data.resize(n);
            residuals_data.resize(n);
            parameters_snapshots.resize(n);
            symbols_to_look_at.resize(n, false);
            mutexes.reserve(n);
            for (size_t i = 0; i < n; ++i)
                mutexes.emplace_back(std::make_unique<std::shared_mutex>());
        }

        // Appelé par les workers — écrase le dernier snapshot
        void update(int symbol_index, std::vector<double> intensities, double /*timestamp*/) {
            if (symbol_index < 0 || symbol_index >= (int)live_data.size()) return;
            std::unique_lock lock(*mutexes[symbol_index]);
            live_data[symbol_index].intensities = std::move(intensities);
            live_data[symbol_index].symbol      = symbols[symbol_index];
        }

        // Appelé par les workers — ajoute un résidu et la valeur d'EWMA associée dans le bucket de la source concernée
        void update_residuals_analysis(int symbol_index, int source_idx, double residual, double ewma) {
            if (symbol_index < 0 || symbol_index >= (int)live_data.size()) return;
            if (residual <= 0.0) return;
            std::unique_lock lock(*mutexes[symbol_index]);
            auto& sources = residuals_data[symbol_index].residuals_by_source;
            auto& ewma_sources = residuals_data[symbol_index].ewma_by_source;
            if (source_idx >= (int)sources.size())
                sources.resize(source_idx + 1);
            if (source_idx >= (int)ewma_sources.size())
                ewma_sources.resize(source_idx + 1);
            auto& vec = sources[source_idx];
            auto& ewma_vec = ewma_sources[source_idx];
            vec.push_back(residual);
            ewma_vec.push_back(ewma);
            // Élagage peu fréquent : ne se déclenche que tous les MAX_RESIDUALS points.
            // Les deux vecteurs grandissent en phase, on les élague donc ensemble pour rester alignés.
            static const size_t MAX_RESIDUALS = 5000;
            if (vec.size() > 2 * MAX_RESIDUALS) {
                vec.erase(vec.begin(), vec.begin() + (int)(vec.size() - MAX_RESIDUALS));
                ewma_vec.erase(ewma_vec.begin(), ewma_vec.begin() + (int)(ewma_vec.size() - MAX_RESIDUALS));
            }
        }

        void update_parameters_snapshot(int symbol_index, const opt_hawkesParams& params, const std::vector<double>& branching_matrix) {
            if (symbol_index < 0 || symbol_index >= (int)live_data.size()) return;
            std::unique_lock lock(*mutexes[symbol_index]);
            parameters_snapshots[symbol_index].params = params;
            parameters_snapshots[symbol_index].branching_matrix = branching_matrix;
        }

        // Appelé par l'UI — lit le dernier snapshot sans le consommer
        Snapshot get_snapshot(int symbol_index) const {
            if (symbol_index < 0 || symbol_index >= (int)live_data.size()) return {};
            std::shared_lock lock(*mutexes[symbol_index]);
            return live_data[symbol_index];
        }

        ResidualsSnapshot get_residuals_snapshot(int symbol_index) const {
            if (symbol_index < 0 || symbol_index >= (int)residuals_data.size()) return {};
            std::shared_lock lock(*mutexes[symbol_index]);
            return residuals_data[symbol_index];
        }

        ParametersSnapshot get_parameters_snapshot(int symbol_index) const {
            if (symbol_index < 0 || symbol_index >= (int)live_data.size()) return {};
            std::shared_lock lock(*mutexes[symbol_index]);
            return parameters_snapshots[symbol_index];
        }

        size_t symbol_count() const { return symbols.size(); }
};