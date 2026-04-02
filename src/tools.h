#pragma once
#include <queue>
#include <mutex>
#include <shared_mutex>
#include <condition_variable>
#include <iostream>
#include <thread>
#include <vector>

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


class TelemetryManager {
    public:
        std::vector<bool> symbols_to_look_at; // Vecteur de booléen pour indiquer quels symboles sont sélectionnés pour la télémétrie

        struct HawkesSnapshot {
            std::vector<double> intensities;
            double timestamp;
        };

        struct Snapshot {
            std::vector<double> intensities;
            double timestamp;
            std::string symbol;
        };

    private:
        std::vector<HawkesSnapshot> live_data;
        std::vector<std::string> symbols;
        // On utilise un tableau de pointeurs uniques pour stabiliser les mutex en mémoire
        std::vector<std::unique_ptr<std::shared_mutex>> mutexes;

    public:
        TelemetryManager(const std::vector<std::string>& syms) : symbols(syms) {
            size_t n = syms.size();
            live_data.resize(n);

            this->symbols_to_look_at.resize(n, false);
            
            // Initialisation correcte des mutex (non copiables)
            mutexes.reserve(n);
            for (size_t i = 0; i < n; ++i) {
                mutexes.emplace_back(std::make_unique<std::shared_mutex>());
            }
        }

        // Appelé par les workers (Ecriture exclusive)
        void update(int symbol_index, std::vector<double> intensities, double timestamp) {
            // std::cout << "Updating telemetry for symbol index " << symbol_index << " at timestamp " << timestamp << std::endl;
            // std::cout << "Intensities: ";
            // for (double & intensity : intensities) {
            //     std::cout << "i:"<<intensity<<std::endl;
            // }
            // std::cout << std::endl;
            if (symbol_index < 0 || symbol_index >= live_data.size()) return;
            // std::cout << "Updated intensity : " << intensities[0] << std::endl;
            // On verrouille en mode exclusif
            std::unique_lock lock(*mutexes[symbol_index]);
            
            // Utilisation de move pour éviter une allocation mémoire lourde
            live_data[symbol_index].intensities = std::move(intensities);
            live_data[symbol_index].timestamp = timestamp;
        }

        // Appelé par l'UI (Lecture partagée)
        Snapshot get_snapshot(int symbol_index) const {
            Snapshot snap;
            if (symbol_index < 0 || symbol_index >= live_data.size()) {
                fprintf(stderr, "invalid symbol_index in TelemetryManager::get_snapshot");
                return snap;
            } 

            // On verrouille en mode partagé (plusieurs lecteurs possibles en même temps)
            std::shared_lock lock(*mutexes[symbol_index]);
            
            snap.intensities = live_data[symbol_index].intensities;
            snap.timestamp   = live_data[symbol_index].timestamp;
            snap.symbol      = symbols[symbol_index];

            // if (snap.intensities.empty() || !snap.timestamp) {
            //     fprintf(stderr, "Empty variables in get_snapshot!");
            // }

            return snap;
        }

        std::vector<Snapshot> get_all_snapshots() const {
            size_t n = symbols.size();
            std::vector<Snapshot> results;
            results.reserve(n); // Allocation unique pour tous les actifs

            for (size_t i = 0; i < n; ++i) {
                // On réutilise la logique de verrouillage partagé
                std::shared_lock lock(*mutexes[i]);
                
                // On construit l'objet directement dans le vecteur
                results.emplace_back(Snapshot{
                    live_data[i].intensities, // Copie du vecteur d'intensités
                    live_data[i].timestamp,
                    symbols[i]
                });
            }
            return results;
        }

        void print_snapshot(int i) {

        };
};