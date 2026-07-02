#ifndef USER_INTERFACE_H
#define USER_INTERFACE_H

#include <cstdio>
#include <cmath>
#include <string>
#include <iostream>
#include <GLFW/glfw3.h>
#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"
#include "implot.h"
#include "../tools.h"
#include "../scheduler.h"

/// Constantes globales de configuration de l'interface (dimensions, titres, listes déroulantes, etc.)
namespace DefaultParameters {
    static constexpr int WindowWidth  = 1280;   ///< Largeur initiale de la fenêtre principale (pixels)
    static constexpr int WindowHeight = 720;    ///< Hauteur initiale de la fenêtre principale (pixels)
    static const std::string title    = "Bloomberg Like Terminal";

    static constexpr int CALIBRATION_TIME = 10;  ///< Durée de calibration par défaut (secondes)
    static constexpr int TRAINING_TIME    = 30;  ///< Durée d'entraînement par défaut (secondes)
    static constexpr int UPDATE_SPEED     = 60;  ///< Fréquence de rafraîchissement cible (FPS)
    static constexpr int N_MAX_WORKERS    = 2;   ///< Nombre de workers Hawkes par défaut

    /// Index 0 = "All" (agrégé), indices 1-N = exchanges individuels
    static const char* websockets[] = {"All", "Binance", "Coinbase", "Kraken", "Bybit", "OKX"};
    static const char* models[]     = {"Hawkes", "LSTM", "Transformer"};

    static const std::string default_symbol   = "BTCUSD"; ///< Symbole sélectionné au démarrage
    static const std::string current_symbol   = "All";
    static const std::string current_exchange = "All";
}

/// Données d'un instrument affiché dans le ticker tape (nom, prix courant, variation)
struct TickerItem {
    std::string name;   ///< Identifiant du symbole (ex: "BTCUSD")
    double price;       ///< Dernier prix observé
    double change;      ///< Variation relative depuis l'ouverture
};

/**
 * Classe de base de l'interface graphique.
 *
 * Gère la fenêtre GLFW/OpenGL, le contexte ImGui/ImPlot et l'ensemble
 * des panneaux de rendu. Les sous-classes spécialisent render_instrument_panel()
 * selon le modèle affiché.
 */
class UserInterface {
    protected:
        SchedulerConfig&    config;             ///< Configuration du scheduler (symboles, paramètres)
        TelemetryManager&   telemetry_manager;  ///< Source des snapshots d'intensités en temps réel
        GLFWwindow*         window;             ///< Handle de la fenêtre GLFW

        /// Index dans config.symbols du symbole affiché par défaut
        int current_symbol_index = config.symbols_map[DefaultParameters::default_symbol].asInt();

        /// Une entrée par symbole : true = affiché dans les graphiques
        std::vector<bool> is_symbol_selected;

        double branching_matrix_color_power_coeff = 4.0; // Coefficient pour ajuster la sensibilité de la couleur du fond dans le tableau de la matrice de branchement

        /// Nombre de frames par seconde cible, modifiable depuis le panneau de contrôle
        int update_speed = DefaultParameters::UPDATE_SPEED;

        /**
         * Buffer circulaire de points (x, y) pour un graphique défilant en temps réel.
         * Quand la capacité MaxSize est atteinte, les anciens points sont écrasés
         * via Offset (ring buffer), ce qui évite toute réallocation.
         */
        struct ScrollingBuffer {
            int MaxSize;            ///< Capacité maximale du buffer
            int Offset;             ///< Indice d'écriture courant dans le ring buffer
            ImVector<ImVec2> Data;  ///< Points stockés (x = temps ImGui, y = valeur)

            ScrollingBuffer(int max_size = 2000) {
                MaxSize = max_size;
                Offset  = 0;
                Data.reserve(MaxSize);
            }

            /// Ajoute un point ; écrase le plus ancien si le buffer est plein
            void AddPoint(float x, float y) {
                if (Data.size() < MaxSize)
                    Data.push_back(ImVec2(x, y));
                else {
                    Data[Offset] = ImVec2(x, y);
                    Offset = (Offset + 1) % MaxSize;
                }
            }

            void Erase() {
                if (Data.size() > 0) {
                    Data.shrink(0);
                    Offset = 0;
                }
            }
        };

        /// symbol → liste de ScrollingBuffer (un buffer par exchange/websocket)
        std::map<std::string, std::vector<ScrollingBuffer>> all_buffers;

        /// Max caché par buffer (symbol → exchange) — mis à jour O(1) par frame,
        /// rescané toutes les ~120 frames pour corriger quand l'ancien max sort de la fenêtre
        std::map<std::string, std::vector<float>> all_buf_max;

        /// Compteur pour le rescan périodique du max visible
        int rescan_frame_counter = 0;

        /// Durée de la fenêtre visible (secondes), partagée entre update et rendu
        float intensity_history = 10.0f;

        /**
         * Buffer glissant qui réinitialise les données quand x repasse à zéro
         * (graphique "rolling window" de durée Span secondes).
         * Contrairement à ScrollingBuffer, l'axe X est relatif à la fenêtre courante.
         */
        struct RollingBuffer {
            float Span;            ///< Durée de la fenêtre glissante (secondes)
            ImVector<ImVec2> Data;

            RollingBuffer() {
                Span = 10.0f;
                Data.reserve(2000);
            }

            /// Ajoute un point ; efface tout le buffer si x a fait un tour (x < dernier x)
            void AddPoint(float x, float y) {
                float xmod = fmodf(x, Span);
                if (!Data.empty() && xmod < Data.back().x)
                    Data.shrink(0);
                Data.push_back(ImVec2(xmod, y));
            }
        };

    public:
        UserInterface(SchedulerConfig& config, TelemetryManager& telemetry_manager);

        void ApplyBloombergStyle();

        void LoadTerminalFont();

        void ApplyPlotStyle();

        /// Crée la fenêtre GLFW et configure le contexte OpenGL. Retourne 0 si succès, 1 en cas d'erreur.
        int initialize();

        /// Affiche la top bar avec le nom du modèle et l'horloge temps réel (coin droit). Un bouton kill switch est prévu pour arrêter le scheduler. Les websockets actifs et un déroulé pour sélectionner un symbole sont également affichés.
        void render_main_bar();

        /// Appelée chaque frame pour appliquer les mises à jour de modèles déclenchées par l'UI.
        void update_hawkes_models();

        /// Graphique défilant des intensités λ(t) pour les symboles sélectionnés pour le websocket source_idx.
        void render_scrolling_buffer(int source_idx);

        /// Barre de titre fixe avec nom du dashboard et horloge temps réel (coin droit).
        void render_title_bar();

        /// Barre de sélection (exchange, modèle, symbole) fixée en haut de la fenêtre.
        void render_selector_bar();

        /// Panneau latéral de contrôle : paramètres Hawkes, calibration, entraînement, symboles.
        void render_control_panel();

        /// Panneau d'instrument spécifique au modèle (redéfini dans les sous-classes).
        void render_instrument_panel();

        /// Panneau de logs avec filtre, auto-scroll et bouton de copie.
        void render_log_panel();

        /// Bande défilante de prix (ticker tape) — non implémentée.
        void render_ticker_tape();

        /// Arborescence de sélection des symboles dans le panneau de contrôle.
        void render_symbol_selector();

        /// Graphique QQ-plot pour évaluer la qualité de calibration du modèle.
        void render_qq_plot();

        /// Boucle de mise à jour de tous les modèles (appelée depuis le thread principal).
        void main_updater();

        void update_intensity_buffers();
        void render_exchange_strip(int source_idx);
        void render_intensities_plot();
        void render_branching_matrix();
        void render_parameters_panel();
        void render_ticker();

        /**
         * Lance la boucle de rendu principale (ImGui + OpenGL).
         * Bloque jusqu'à la fermeture de la fenêtre par l'utilisateur.
         * Retourne 0 à la sortie normale.
         */
        int main_renderer();
};

/// Spécialisation de l'interface pour le modèle de Hawkes (surcharge render_instrument_panel).
class HawkesInterface : public UserInterface {
    protected:

    public:
        HawkesInterface(SchedulerConfig& config, TelemetryManager& telemetry_manager)
            : UserInterface(config, telemetry_manager) {}

        void render_instrument_panel();
};

/// Spécialisation de l'interface pour le menu principal (à implémenter).
class MainMenuInterface : public UserInterface {
    protected:

    public:
        MainMenuInterface(SchedulerConfig& config, TelemetryManager& telemetry_manager)
            : UserInterface(config, telemetry_manager) {}
};

#endif // USER_INTERFACE_H
