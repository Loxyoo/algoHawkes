#pragma once
#include <queue>
#include <mutex>
#include <shared_mutex>
#include <condition_variable>
#include <iostream>
#include <thread>
#include <vector>
#include <algorithm>
#include <functional>   // std::less

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

constexpr int DEFAULT_CIRCULAR_BUFFER_SIZE = 100;

// CircularBuffer est une classe générique pour un buffer circulaire de taille fixe.
template <typename T> class CircularBuffer {
    protected :
        std::vector<T> buffer;
        size_t head;
        size_t tail;
        size_t max_size;
        bool full;

    public :
        CircularBuffer(size_t size) : 
            buffer(size), 
            head(0), tail(0), 
            max_size(size), 
            full(false) {}
            
        CircularBuffer() : 
            buffer(DEFAULT_CIRCULAR_BUFFER_SIZE), 
            head(0), tail(0), 
            max_size(DEFAULT_CIRCULAR_BUFFER_SIZE), 
            full(false) {}

        // Renvoie true si le buffer circulaire est vide, false sinon.
        bool empty() const {return (!full && (head == tail));}
        // Renvoie true si le buffer circulaire est plein, false sinon.
        bool is_full() const {return full;}
        // Renvoie la capacité maximale du buffer circulaire.
        size_t capacity() const {return max_size;}

        // Ajoute un élément au buffer circulaire. Si le buffer est plein, il écrase l'élément le plus ancien.
        void put(T item) {
            buffer[head] = item;
            if (full) {
                tail = (tail + 1) % max_size; // Overwrite the oldest data
            }
            head = (head + 1) % max_size;
            full = head == tail;
        }

        // Renvoie le plus ancien élément (celui qui sera écrasé au prochain put quand le
        // buffer est plein) SANS le retirer. Ne doit être appelé que si le buffer est non vide.
        // Utile pour les métriques online sur fenêtre glissante : avant d'écraser le plus
        // ancien résidu, on peut le retirer de son bin.
        const T& peek_oldest() const { return buffer[tail]; }

        // Renvoie la valeur en tête du buffer circulaire et l'enlève du buffer si et seulement si le buffer est plein.
        T get() {
            if (empty()) {
                throw std::runtime_error("Buffer is empty");
            }
            T item = buffer[tail];
            full = false;
            tail = (tail + 1) % max_size;
            return item;
        }

        // Renvoie le nombre d'éléments actuellement stockés dans le buffer circulaire.
        size_t size() const {
            size_t size = max_size;
            if(!full) {
                if(head >= tail) {
                    size = head - tail;
                } else {
                    size = max_size + head - tail;
                }
            }
            return size;
        }

        // Permet de redimensionner le buffer circulaire. Si la nouvelle taille est inférieure à la taille actuelle, la fonction échoue et renvoie false. Sinon, elle redimensionne le buffer et renvoie true.
        bool change_size(size_t new_size) {
            if (new_size < size()) {
                return false; // Cannot shrink below current size
            }
            std::vector<T> new_buffer(new_size);
            size_t current_size = size();
            for (size_t i = 0; i < current_size; ++i) {
                new_buffer[i] = buffer[(tail + i) % max_size];
            }
            buffer = std::move(new_buffer);
            head = current_size;
            tail = 0;
            max_size = new_size;
            full = false;
            return true;
        }

        // Renvoie une copie du buffer
        std::vector<T> get_all_items() {return buffer;}

        // Renvoie les éléments dans l'ordre chronologique (du plus ancien au plus récent),
        // contrairement à get_all_items() qui expose l'ordre physique interne du ring.
        // Ce dernier présente une discontinuité au point d'enroulement (le plus récent est
        // à head-1, le plus ancien à tail) et ne convient donc pas au tracé d'une série
        // temporelle. Ne renvoie que les éléments réellement présents (taille logique),
        // sans le remplissage par défaut du buffer avant qu'il ne soit plein.
        std::vector<T> get_ordered_items() const {
            std::vector<T> ordered;
            const size_t n = size();
            ordered.reserve(n);
            for (size_t i = 0; i < n; ++i)
                ordered.push_back(buffer[(tail + i) % max_size]);
            return ordered;
        }

        // Réinitialise le buffer circulaire en mettant tout les éléments du 
        // buffer à 0 et en mettant les attributs à leurs valeurs initiaux. 
        void clear_buffer() {
            for (int i = 0; i < max_size; i++) {
                buffer[i] = 0.0;
            }
            head = 0;
            tail = 0;
            full = false;
        }
};

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

        struct ResidualsBuffersSnapshot {
            std::vector<CircularBuffer<double>> residualBuffers_by_source;
            std::vector<CircularBuffer<double>> ewmaBuffers_by_sources;
            std::string symbol;
        };

        struct Chi2MetricsSnapshot {
            std::vector<chi2_metrics> chi2metrics_by_sources;
            std::vector<int> W;
        };

        struct ParametersSnapshot {
            opt_hawkesParams params;
            std::vector<double> branching_matrix;
        };

    private:
        std::vector<Snapshot> live_data;
        std::vector<ResidualsSnapshot> residuals_data;
        std::vector<ResidualsBuffersSnapshot> residualBuffers_data;
        std::vector<std::string> symbols;
        std::vector<std::unique_ptr<std::shared_mutex>> mutexes;
        std::vector<ParametersSnapshot> parameters_snapshots; // Snapshot par symbole
        std::vector<Chi2MetricsSnapshot> chi2_metrics_data;
        std::vector<std::vector<double>> event_rates_data; // Taux d'événements (events/s) par symbole, puis par source

    public:
        TelemetryManager(const std::vector<std::string>& syms) : symbols(syms) {
            size_t n = syms.size();
            live_data.resize(n);
            residuals_data.resize(n);
            residualBuffers_data.resize(n);
            parameters_snapshots.resize(n);
            chi2_metrics_data.resize(n);
            event_rates_data.resize(n);
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

        void update_residualsBuffer_analysis(int buffer_size, int symbol_index, int source_idx, double residual, double ewma) {
            if (buffer_size <= 0) return;
            if (symbol_index < 0 || symbol_index >= (int)live_data.size()) return;
            if (source_idx < 0) return;
            if (residual <= 0.0) return;
            std::unique_lock lock(*mutexes[symbol_index]);
            // Les buffers par source ne sont pas dimensionnés à la construction (le TelemetryManager
            // ne connaît que les symboles, pas le nombre de sources). On les fait grandir paresseusement
            // à la première écriture de chaque source, sinon [source_idx] indexe un vecteur vide et
            // renvoie une référence sur l'adresse nulle (segfault dans capacity()/put()).
            auto& residual_sources = residualBuffers_data[symbol_index].residualBuffers_by_source;
            auto& ewma_sources     = residualBuffers_data[symbol_index].ewmaBuffers_by_sources;
            if (source_idx >= (int)residual_sources.size())
                residual_sources.resize(source_idx + 1, CircularBuffer<double>(buffer_size));
            if (source_idx >= (int)ewma_sources.size())
                ewma_sources.resize(source_idx + 1, CircularBuffer<double>(buffer_size));
            auto& residual_circBuffer   = residual_sources[source_idx];
            auto& ewma_circBuffer       = ewma_sources[source_idx];
            // On change la taille du buffer du TM pour qu'elle soit la même que dans l'alpha
            if (buffer_size != residual_circBuffer.capacity()) {
                residual_circBuffer.change_size(buffer_size);
            }
            residual_circBuffer.put(residual);
            ewma_circBuffer.put(ewma);
        }

        void update_chi2metrics(int symbol_index, int source_idx, chi2_metrics chi2_m, std::vector<int> W) {
            if (symbol_index < 0 || symbol_index >= (int)live_data.size()) return;
            if (source_idx < 0) return;
            std::unique_lock lock(*mutexes[symbol_index]);
            // Comme les buffers de résidus, les métriques par source ne sont pas dimensionnées
            // à la construction (le TelemetryManager ne connaît que les symboles, pas le nombre
            // de sources). On les fait grandir paresseusement à la première écriture de chaque
            // source, sinon [source_idx] indexe un vecteur vide → déréférencement de l'adresse
            // nulle (segfault).
            auto& sources = chi2_metrics_data[symbol_index].chi2metrics_by_sources;
            if (source_idx >= (int)sources.size())
                sources.resize(source_idx + 1);
            auto& m = sources[source_idx];
            if (m.K != chi2_m.K) {
                m.K = chi2_m.K;
                m.bins = chi2_m.bins;
                m.counter_in_bins = chi2_m.counter_in_bins;
                m.chi2_statistic = chi2_m.chi2_statistic;
                m.chi2_critical_value = chi2_m.chi2_critical_value;
                chi2_metrics_data[symbol_index].W = W;
            } else {
                m.chi2_statistic = chi2_m.chi2_statistic;
            }

        }

        // Appelé par les workers — écrase le vecteur des taux d'événements (events/s) par source.
        // Le worker publie le vecteur complet à chaque frame (les sources silencieuses y décroissent
        // vers 0), donc une simple affectation suffit : pas de croissance paresseuse par source ici.
        void update_event_rates(int symbol_index, const std::vector<double>& rates) {
            if (symbol_index < 0 || symbol_index >= (int)event_rates_data.size()) return;
            std::unique_lock lock(*mutexes[symbol_index]);
            event_rates_data[symbol_index] = rates;
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

        ResidualsBuffersSnapshot get_residualsBuffers_snapshot(int symbol_index) const {
            if (symbol_index < 0 || symbol_index >= (int)residualBuffers_data.size()) return {};
            std::shared_lock lock(*mutexes[symbol_index]);
            return residualBuffers_data[symbol_index];
        }

        ParametersSnapshot get_parameters_snapshot(int symbol_index) const {
            if (symbol_index < 0 || symbol_index >= (int)live_data.size()) return {};
            std::shared_lock lock(*mutexes[symbol_index]);
            return parameters_snapshots[symbol_index];
        }

        Chi2MetricsSnapshot get_chi2metrics_snapshot(int symbol_index) const {
            if (symbol_index < 0 || symbol_index >= (int)chi2_metrics_data.size()) return {};
            std::shared_lock lock(*mutexes[symbol_index]);
            return chi2_metrics_data[symbol_index];
        }

        // Appelé par l'UI — lit le dernier vecteur de taux d'événements (events/s) par source.
        std::vector<double> get_event_rates_snapshot(int symbol_index) const {
            if (symbol_index < 0 || symbol_index >= (int)event_rates_data.size()) return {};
            std::shared_lock lock(*mutexes[symbol_index]);
            return event_rates_data[symbol_index];
        }

        size_t symbol_count() const { return symbols.size(); }
};

// TemporalBuffer est une classe générique pour un buffer contenant des éléments avec un timestamp associé. 
// Le delta temps entre le premier et le dernier élément ajouter et toujours inférieur ou égal à une durée 
// fixe (max_duration). Quand un nouvel élément est ajouté, les éléments les plus anciens sont supprimés pour 
// maintenir la contrainte de durée.
// Le type générique T est une structure.
template <typename T> class TemporalBuffer {
    protected :
        std::queue<T> buffer;
        std::function<bool(const T&, const T&)> compare; // Fonction de comparaison pour déterminer l'ordre des éléments
        double max_duration; // Durée maximale entre le premier et le dernier élément du buffer
    
    public :
        TemporalBuffer(double duration, std::function<bool(const T&, const T&)> compare_func) : max_duration(duration), compare(compare_func) {}
        void put(T item);
        T get();
};

// DelayedBuffer est une classe générique pour un buffer contenant des éléments avec un timestamp associé.
// Chaque élément est stocké pendant une durée fixe (delay_duration).
// Exemple d'utilisation : retarder l'arrivée des données dans l'alpha du système afin de sumuler de la latence réseau.
// Cette classe devra être initialisé sur un thread à part.
template <typename T> class DelayedBuffer {
    protected :
        std::queue<T> buffer;
        std::function<bool(const T&, const T&)> compare; // Fonction de comparaison pour déterminer l'ordre des éléments
        double delay_duration; // Durée de retard
    
    public :
};

/// @brief Buffer circulaire de taille fixe qui maintient les données de type générique T triées selon la fonction de comparaison compare 
/// @tparam T T doit etre move-assignable sans exception (hot path HFT)
/// @tparam Compare Fonction de comparaison
template <typename T, typename Compare = std::less<T>>
class SortedCircularBuffer {
    static_assert(std::is_nothrow_move_assignable<T>::value,
                  "T doit etre move-assignable sans exception (hot path HFT)");

    std::vector<T> fifo_;    // ordre d'arrivée (ring)
    std::vector<T> sorted_;  // ordre trié, contigu
    std::size_t    head_  = 0;  // index du plus ancien dans fifo_
    std::size_t    count_ = 0;
    std::size_t    cap_;
    Compare        cmp_;

public:
    explicit SortedCircularBuffer(std::size_t capacity, Compare cmp = Compare{})
        : fifo_(capacity), cap_(capacity), cmp_(cmp)
    {
        sorted_.reserve(capacity);   // plus jamais d'allocation ensuite
    }

    // ------------------------------------------------------------------
    // put : insère `item`. Si le buffer est plein, évince le plus ancien.
    // Renvoie true et écrit l'évincé dans *evicted (si non-null) quand une
    // éviction a eu lieu — utile pour les métriques online incrémentales
    // (ex. mettre à jour une somme : sum += item - evicted).
    // ------------------------------------------------------------------
    bool put(const T& item, T* evicted = nullptr) noexcept {
        if (count_ < cap_) {                       // phase de warm-up seulement
            std::size_t pos = head_ + count_;
            if (pos >= cap_) pos -= cap_;          // pas de modulo
            fifo_[pos] = item;
            ++count_;
            T* base = sorted_.data();
            T* it   = std::lower_bound(base, base + sorted_.size(), item, cmp_);
            sorted_.insert(sorted_.begin() + (it - base), item);  // capacité déjà réservée
            return false;
        }

        // --- Régime permanent : éviction + insertion en UNE rotation ---
        const T oldest = fifo_[head_];
        if (evicted) *evicted = oldest;
        fifo_[head_] = item;
        if (++head_ == cap_) head_ = 0;            // incrément conditionnel, pas de %

        T* base = sorted_.data();
        const std::size_t n     = cap_;
        const std::size_t i_old =
            std::lower_bound(base, base + n, oldest, cmp_) - base;
        const std::size_t i_new =
            std::lower_bound(base, base + n, item,   cmp_) - base;

        if (i_new > i_old) {
            // le nouveau va plus à droite : on décale le bloc (i_old+1 .. i_new-1)
            // d'une case vers la gauche, écrasant l'évincé, puis on pose item.
            std::move(base + i_old + 1, base + i_new, base + i_old);
            base[i_new - 1] = item;
        } else {
            // le nouveau va à gauche (ou à la même place) : décalage à droite.
            std::move_backward(base + i_new, base + i_old, base + i_old + 1);
            base[i_new] = item;
        }
        return true;
    }

    // ------------------------------------------------------------------
    // Accès en lecture — tout est O(1)
    // ------------------------------------------------------------------
    bool        empty()    const noexcept { return count_ == 0; }
    bool        is_full()  const noexcept { return count_ == cap_; }
    std::size_t size()     const noexcept { return count_; }
    std::size_t capacity() const noexcept { return cap_; }

    // k-ième order statistic (0 = min, size()-1 = max)
    const T& sorted_at(std::size_t k) const noexcept { return sorted_[k]; }
    const T& min() const noexcept { return sorted_.front(); }
    const T& max() const noexcept { return sorted_.back();  }

    // plus ancien / plus récent dans l'ordre d'arrivée
    const T& oldest() const noexcept { return fifo_[head_]; }
    const T& newest() const noexcept {
        std::size_t pos = head_ + count_ - 1;
        if (pos >= cap_) pos -= cap_;
        return fifo_[pos];
    }

    // Médiane (moyenne des deux du milieu si taille paire) — nécessite non-vide.
    T median() const noexcept {
        const std::size_t m = count_ / 2;
        if (count_ & 1) return sorted_[m];
        return (sorted_[m - 1] + sorted_[m]) / T(2);
    }

    // Quantile empirique par interpolation linéaire, q dans [0,1].
    T quantile(double q) const noexcept {
        const double      h  = q * double(count_ - 1);
        const std::size_t lo = static_cast<std::size_t>(h);
        const std::size_t hi = (lo + 1 < count_) ? lo + 1 : lo;
        const double      w  = h - double(lo);
        return static_cast<T>(sorted_[lo] + w * (sorted_[hi] - sorted_[lo]));
    }

    // Vue triée complète (pour itérer sans copie)
    const T* sorted_data() const noexcept { return sorted_.data(); }
};

/// @brief Renvoie le quantile d'ordre alpha de la distribution exponentielle de paramètre lambda.
/// @param alpha ordre du quantile.
/// @param lambda paramètre de la distribution exponentielle.
inline double exponential_quantile(double alpha, double lambda) {
    return -std::log(1-alpha) / lambda;
}