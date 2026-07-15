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
#include "UIBuffers.h"
#include "../data_simulation.h"

/// Constantes globales de configuration de l'interface (dimensions, titres, listes déroulantes, etc.)
namespace DefaultParameters {
    static constexpr int WindowWidth  = 1280;   ///< Largeur initiale de la fenêtre principale (pixels)
    static constexpr int WindowHeight = 720;    ///< Hauteur initiale de la fenêtre principale (pixels)
    static const std::string title    = "Bloomberg Like Terminal";

    static constexpr int CALIBRATION_TIME = 10;  ///< Durée de calibration par défaut (secondes)
    static constexpr int TRAINING_TIME    = 30;  ///< Durée d'entraînement par défaut (secondes)
    static constexpr int UPDATE_SPEED     = 60;  ///< Fréquence de rafraîchissement cible (FPS)
    static constexpr int N_MAX_WORKERS    = 2;   ///< Nombre de workers Hawkes par défaut

    /// Index 0 = "All" (agrégé), indices 1-N = exchanges individuels.
    /// Défini dans user_interface.cpp et modifiable au runtime (ex : renseigné
    /// dynamiquement avec les exchanges fictifs du stress test).
    extern std::vector<std::string> websockets;
    static const char* models[]     = {"Hawkes", "LSTM", "Transformer"};

    extern std::string default_symbol; ///< Symbole sélectionné au démarrage (défini dans user_interface.cpp)
    static const std::string current_symbol   = "All";
    static const std::string current_exchange = "All";

    /// Une entrée par symbole : true = affiché dans les graphiques
    /// Initialisé dans le constructeur de UserInterface 
    extern std::vector<bool> is_symbol_selected;

    #ifdef STRESS_TEST

    extern std::vector<SimOgataParams> all_OgataParams;

    #endif
}

/// Déclaration anticipée : UserInterface ne stocke que des pointeurs UI_Panel*,
/// la définition complète de la classe vient plus bas dans ce fichier.
class UI_Panel;

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

        std::vector<UI_Panel*> panels; ///< Liste des panneaux de rendu (control, analytics, logs, etc.)
    public:
        UserInterface(SchedulerConfig& config, TelemetryManager& telemetry_manager);
        /// Libère les panneaux alloués dynamiquement par create_default_panels_v2.
        ~UserInterface();

        void ApplyBloombergStyle();

        void LoadTerminalFont();

        void ApplyPlotStyle();

        /// Crée la fenêtre GLFW et configure le contexte OpenGL. Retourne 0 si succès, 1 en cas d'erreur.
        int initialize();

        /// Barre de titre fixe avec nom du dashboard et horloge temps réel (coin droit).
        void render_title_bar();

        /// Bande défilante de prix (ticker tape) — non implémentée.
        void render_ticker_tape();

        /// Boucle de mise à jour de tous les modèles (appelée depuis le thread principal).
        void UI_renderer();

        /**
         * Lance la boucle de rendu principale (ImGui + OpenGL).
         * Bloque jusqu'à la fermeture de la fenêtre par l'utilisateur.
         * Retourne 0 à la sortie normale.
         */
        int main_renderer();
};

class UI_Panel {
    protected :
        SchedulerConfig&    config;             ///< Configuration du scheduler (symboles, paramètres)
        TelemetryManager&   telemetry_manager;  ///< Source des snapshots d'intensités en temps réel

        /// Index dans config.symbols du symbole affiché par défaut
        int current_symbol_index = config.symbols_map[DefaultParameters::default_symbol].asInt();
    public :
        UI_Panel(SchedulerConfig& config, TelemetryManager& telemetry_manager)
            : config(config), telemetry_manager(telemetry_manager) {}

        /// Destructeur virtuel : indispensable car les panneaux sont détruits
        /// via des pointeurs UI_Panel* (sinon comportement indéfini).
        virtual ~UI_Panel() = default;

        // Méthode qui devra être définite par les classes filles
        virtual void render() = 0;
};

class Hawkes_Panel : public UI_Panel {
    public :
        Hawkes_Panel(SchedulerConfig& config, TelemetryManager& telemetry_manager)
            : UI_Panel(config, telemetry_manager) {}

        virtual void render() = 0;
};

class LogPanel : public Hawkes_Panel {
    public :
        LogPanel(SchedulerConfig& config, TelemetryManager& telemetry_manager)
            : Hawkes_Panel(config, telemetry_manager) {}

        /// Panneau de logs avec filtre, auto-scroll et bouton de copie.
        void render();
};

class AnalyticsPanel : public Hawkes_Panel {
    protected : 
        /// Compteur pour le rescan périodique du max visible
        int rescan_frame_counter = 0;
        /// Durée de la fenêtre visible (secondes), partagée entre update et rendu
        float intensity_history = 10.0f;

        /// symbol → liste de ScrollingBuffer (un buffer par exchange/websocket)
        std::map<std::string, std::vector<ScrollingBuffer>> all_buffers;

        /// Max caché par buffer (symbol → exchange) — mis à jour O(1) par frame,
        /// rescané toutes les ~120 frames pour corriger quand l'ancien max sort de la fenêtre
        std::map<std::string, std::vector<float>> all_buf_max;

        double branching_matrix_color_power_coeff = 4.0; // Coefficient pour ajuster la sensibilité de la couleur du fond dans le tableau de la matrice de branchement
    public :
        AnalyticsPanel(SchedulerConfig& config, TelemetryManager& telemetry_manager)
            : Hawkes_Panel(config, telemetry_manager) {
                // Pré-alloue un vecteur de buffers vide pour chaque symbole connu
                for (const std::string& symbol : config.symbols) {
                    all_buffers[symbol] = std::vector<ScrollingBuffer>();
                }
            }

        void render_exchange_strip(int source_idx);

        /// Mets à jour les buffers d'intensité pour tous les symboles et exchanges
        void update_intensity_buffers();

        /// Graphique QQ-plot pour évaluer la qualité de calibration du modèle.
        void render_qq_plot();

        /// Réalise le rendu du panneau de la matrice de branchement (branching matrix) pour le modèle de Hawkes
        void render_branching_matrix(std::vector<double> branching_matrix);

        /// Réalise le rendu des graphiques d'intensité pour tous les symboles sélectionnées et exchanges
        void render_intensities_plot();

        /// Graphique défilant des intensités λ(t) pour les symboles sélectionnés pour le websocket source_idx.
        void render_scrolling_buffer(int source_idx);


        /// @brief Main renderer for the Hawkes analytics panel
        void render();
};

class ControlPanel : public Hawkes_Panel {
    protected :
        /// Nombre de frames par seconde cible, modifiable depuis le panneau de contrôle
        int update_speed = DefaultParameters::UPDATE_SPEED;
    public :
        ControlPanel(SchedulerConfig& config, TelemetryManager& telemetry_manager)
            : Hawkes_Panel(config, telemetry_manager) {}

        /// Barre de commande. Permet de naviguer entre les exchanges, modèles, symboles plus facilement.
        void render_command_bar();
        
        /// Arborescence de sélection des symboles dans le panneau de contrôle.
        void render_symbol_selector();

        /// Panneau latéral de contrôle : paramètres Hawkes, calibration, entraînement, symboles.
        void render_control_panel();

        /// Graphique temps réel de l'EWMA des résidus du compensateur de Hawkes (une courbe par source).
        /// Métrique de qualité du modèle : elle doit osciller autour de 1 (résidus ~ Exp(1)).
        void render_residuals_ewma();

        /// Affiche la top bar avec le nom du modèle et l'horloge temps réel (coin droit). Un bouton kill switch est prévu pour arrêter le scheduler. Les websockets actifs et un déroulé pour sélectionner un symbole sont également affichés.
        void render_main_bar();

        void render();
};

std::vector<UI_Panel*> create_default_panels_v2(SchedulerConfig& config, TelemetryManager& telemetry_manager);

#endif // USER_INTERFACE_H


