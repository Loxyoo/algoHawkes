#include "user_interface.h"

// Définition unique des paramètres modifiables au runtime déclarés `extern` dans l'en-tête.
namespace DefaultParameters {
    std::vector<std::string> websockets = {"All", "Binance", "Coinbase", "Kraken", "Bybit", "OKX"};
    std::string default_symbol = "BTCUSD";
    std::vector<bool> is_symbol_selected; // Redimensionné dans le constructeur de UserInterface
#ifdef STRESS_TEST
    std::vector<SimOgataParams> all_OgataParams; // Rempli dans main.cpp après la simulation
#endif
}

static void glfw_error_callback(int error, const char* description) {
    fprintf(stderr, "Glfw Error %d: %s\n", error, description);
}

UserInterface::UserInterface(SchedulerConfig& config, TelemetryManager& telemetry_manager)
    : config(config), telemetry_manager(telemetry_manager)
{
    DefaultParameters::is_symbol_selected.resize(config.symbols.size(), false);
    // Active le symbole par défaut à l'ouverture
    DefaultParameters::is_symbol_selected[config.symbols_map[DefaultParameters::default_symbol].asInt()] = true;

    this->panels = create_default_panels_v2(config, telemetry_manager);
}

UserInterface::~UserInterface() {
    for (UI_Panel* panel : this->panels) {
        delete panel;
    }
}

void UserInterface::ApplyBloombergStyle() {
    ImGuiStyle& style = ImGui::GetStyle();
    ImVec4* colors = style.Colors;

    // ---- Palette de base (ambre Bloomberg sur fond noir) ----
    const ImVec4 BG_DEEP    = ImVec4(0.00f, 0.00f, 0.00f, 1.00f); // noir
    const ImVec4 BG_PANEL   = ImVec4(0.05f, 0.06f, 0.08f, 1.00f); // panneaux
    const ImVec4 BG_HOVER   = ImVec4(0.10f, 0.12f, 0.16f, 1.00f);
    const ImVec4 BORDER     = ImVec4(1.00f, 1.00f, 1.00f, 1.00f); // blanc
    const ImVec4 AMBER      = ImVec4(1.00f, 0.65f, 0.00f, 1.00f); // ambre signature
    const ImVec4 AMBER_DIM  = ImVec4(0.80f, 0.52f, 0.00f, 1.00f);
    const ImVec4 TEXT_MAIN  = ImVec4(0.78f, 0.85f, 0.91f, 1.00f); // gris clair
    const ImVec4 TEXT_DIM   = ImVec4(0.45f, 0.50f, 0.58f, 1.00f);
    const ImVec4 GREEN      = ImVec4(0.30f, 0.80f, 0.31f, 1.00f); // up / ok
    const ImVec4 RED        = ImVec4(0.89f, 0.29f, 0.29f, 1.00f); // down / err

    colors[ImGuiCol_Text]                 = TEXT_MAIN;
    colors[ImGuiCol_TextDisabled]         = TEXT_DIM;
    colors[ImGuiCol_WindowBg]             = BG_DEEP;
    colors[ImGuiCol_ChildBg]              = BG_PANEL;
    colors[ImGuiCol_PopupBg]              = BG_PANEL;
    colors[ImGuiCol_Border]               = BORDER;
    colors[ImGuiCol_BorderShadow]         = ImVec4(0,0,0,0);
    colors[ImGuiCol_FrameBg]              = BG_PANEL;
    colors[ImGuiCol_FrameBgHovered]       = BG_HOVER;
    colors[ImGuiCol_FrameBgActive]        = BG_HOVER;
    colors[ImGuiCol_TitleBg]              = ImVec4(0.04f,0.08f,0.13f,1.00f);
    colors[ImGuiCol_TitleBgActive]        = ImVec4(0.07f,0.14f,0.23f,1.00f);
    colors[ImGuiCol_TitleBgCollapsed]     = BG_DEEP;
    colors[ImGuiCol_MenuBarBg]            = ImVec4(0.04f,0.08f,0.13f,1.00f);
    colors[ImGuiCol_ScrollbarBg]          = BG_DEEP;
    colors[ImGuiCol_ScrollbarGrab]        = BORDER;
    colors[ImGuiCol_ScrollbarGrabHovered] = AMBER_DIM;
    colors[ImGuiCol_ScrollbarGrabActive]  = AMBER;
    colors[ImGuiCol_CheckMark]            = AMBER;
    colors[ImGuiCol_SliderGrab]           = AMBER_DIM;
    colors[ImGuiCol_SliderGrabActive]     = AMBER;
    colors[ImGuiCol_Button]               = BG_PANEL;
    colors[ImGuiCol_ButtonHovered]        = BG_HOVER;
    colors[ImGuiCol_ButtonActive]         = ImVec4(0.20f,0.13f,0.00f,1.00f);
    colors[ImGuiCol_Header]               = ImVec4(0.07f,0.14f,0.23f,1.00f);
    colors[ImGuiCol_HeaderHovered]        = BG_HOVER;
    colors[ImGuiCol_HeaderActive]         = ImVec4(0.10f,0.20f,0.32f,1.00f);
    colors[ImGuiCol_Separator]            = BORDER;
    colors[ImGuiCol_SeparatorHovered]     = AMBER_DIM;
    colors[ImGuiCol_SeparatorActive]      = AMBER;
    colors[ImGuiCol_ResizeGrip]           = BORDER;
    colors[ImGuiCol_ResizeGripHovered]    = AMBER_DIM;
    colors[ImGuiCol_ResizeGripActive]     = AMBER;
    colors[ImGuiCol_Tab]                  = ImVec4(0.04f,0.08f,0.13f,1.00f);
    colors[ImGuiCol_TabHovered]           = BG_HOVER;
    colors[ImGuiCol_TabActive]            = ImVec4(0.07f,0.14f,0.23f,1.00f);
    colors[ImGuiCol_TabUnfocused]         = BG_DEEP;
    colors[ImGuiCol_TabUnfocusedActive]   = BG_PANEL;
    colors[ImGuiCol_PlotLines]            = AMBER;
    colors[ImGuiCol_PlotLinesHovered]     = ImVec4(1.00f,0.80f,0.30f,1.00f);
    colors[ImGuiCol_PlotHistogram]        = AMBER;
    colors[ImGuiCol_PlotHistogramHovered] = ImVec4(1.00f,0.80f,0.30f,1.00f);
    colors[ImGuiCol_TableHeaderBg]        = ImVec4(0.07f,0.14f,0.23f,1.00f);
    colors[ImGuiCol_TableBorderStrong]    = BORDER;
    colors[ImGuiCol_TableBorderLight]     = ImVec4(0.08f,0.12f,0.18f,1.00f);
    colors[ImGuiCol_TableRowBg]           = BG_DEEP;
    colors[ImGuiCol_TableRowBgAlt]        = ImVec4(0.04f,0.05f,0.07f,1.00f);
    colors[ImGuiCol_TextSelectedBg]       = ImVec4(0.20f,0.13f,0.00f,0.60f);
    colors[ImGuiCol_NavHighlight]         = AMBER;

    // ---- Géométrie : angles nets, style terminal ----
    style.WindowRounding    = 0.0f;
    style.ChildRounding     = 0.0f;
    style.FrameRounding     = 0.0f;
    style.PopupRounding     = 0.0f;
    style.ScrollbarRounding = 0.0f;
    style.GrabRounding      = 0.0f;
    style.TabRounding       = 0.0f;
    style.WindowBorderSize  = 1.0f;
    style.FrameBorderSize   = 1.0f;
    style.ChildBorderSize   = 1.0f;
    style.WindowPadding     = ImVec2(6, 6);
    style.FramePadding      = ImVec2(5, 3);
    style.ItemSpacing       = ImVec2(6, 4);
    style.ItemInnerSpacing  = ImVec2(4, 4);
    style.ScrollbarSize     = 12.0f;
}

void UserInterface::LoadTerminalFont() {
    ImGuiIO& io = ImGui::GetIO();
    // Police monospace — chemin absolu injecté par CMake pour être indépendant du CWD.
#ifdef ALGOICT_SOURCE_DIR
    std::string font_path = std::string(ALGOICT_SOURCE_DIR) + "/src/frontend/fonts/JetBrains_Mono/JetBrainsMono-VariableFont_wght.ttf";
#else
    std::string font_path = "src/frontend/fonts/JetBrains_Mono/JetBrainsMono-VariableFont_wght.ttf";
#endif
    io.Fonts->AddFontFromFileTTF(font_path.c_str(), 15.0f);
    // Ne pas appeler io.Fonts->Build() ici : le backend OpenGL le fait automatiquement à l'init.
}

void UserInterface::ApplyPlotStyle() {
    ImPlotStyle& ps = ImPlot::GetStyle();
    ps.Colors[ImPlotCol_FrameBg]  = ImVec4(0.02f,0.02f,0.03f,1.00f);
    ps.Colors[ImPlotCol_PlotBg]   = ImVec4(0.03f,0.04f,0.06f,1.00f);
    ps.Colors[ImPlotCol_PlotBorder] = ImVec4(1.00f,1.00f,1.00f,1.00f); // blanc
    ps.Colors[ImPlotCol_AxisGrid] = ImVec4(0.10f,0.15f,0.22f,1.00f);
    ps.Colors[ImPlotCol_AxisText] = ImVec4(0.45f,0.50f,0.58f,1.00f);
    ps.PlotPadding = ImVec2(8,8);
    ps.LineWeight  = 1.2f;
}

int UserInterface::initialize() {
    glfwSetErrorCallback(glfw_error_callback);
    if (!glfwInit()) return 1;

    const char* glsl_version = nullptr;

    // macOS impose OpenGL 3.2 Core Profile et forward-compatible context
    #if defined(__APPLE__)
        glsl_version = "#version 150";
        glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
        glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 2);
        glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
        glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
    #else
        glsl_version = "#version 130";
        glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
        glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 0);
    #endif

    this->window = glfwCreateWindow(
        DefaultParameters::WindowWidth,
        DefaultParameters::WindowHeight,
        DefaultParameters::title.c_str(),
        NULL, NULL);
    if (this->window == NULL) return 1;

    glfwMakeContextCurrent(this->window);
    glfwSwapInterval(0); // VSync désactivé — le framerate est géré manuellement via busy-wait

    return 0;
}

void ControlPanel::render_main_bar() {
    if (ImGui::BeginMainMenuBar()) {
        // Affiche le nom du modèle sélectionné
        // Ajout d'un petit cadre autour du nom du modèle pour le mettre en évidence
        ImGui::Text("Multivariate Hawkes Model");
        ImGui::SameLine();

        // Déroulé pour sélectionner un symbole parmi ceux disponibles
        static int selected_symbol = 0;
        std::vector<const char*> symbol_labels;
        for (auto& s : config.symbols) symbol_labels.push_back(s.c_str());
        ImGui::SetNextItemWidth(150);
        ImGui::Combo("Symbole##qq", &selected_symbol, symbol_labels.data(), (int)symbol_labels.size());

        if (!DefaultParameters::is_symbol_selected[selected_symbol]) {
            for (size_t i = 0; i < DefaultParameters::is_symbol_selected.size(); ++i) {
                DefaultParameters::is_symbol_selected[i] = false;
            }
            DefaultParameters::is_symbol_selected[selected_symbol] = true;
        }
        
        this->current_symbol_index = selected_symbol;

        ImGui::SameLine();
        // Déroulé pour affiché les websockets
        static int current_websocket = 0;
        std::vector<const char*> ws_items;
        ws_items.reserve(DefaultParameters::websockets.size());
        for (const std::string& ws : DefaultParameters::websockets) ws_items.push_back(ws.c_str());
        ImGui::SetNextItemWidth(150);
        ImGui::Combo("Websocket##Combo", &current_websocket, ws_items.data(), (int)ws_items.size());


        ImGui::SameLine();
        // Affiche les websockets actifs
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.00f, 1.00f, 0.00f, 1.00f));
        for (const std::string&ws : config.websocket_map.getMemberNames()) {
            ImGui::Text("%s ", ws.c_str());
            ImGui::SameLine();
        }
        ImGui::PopStyleColor(1);

        // Kill switch pour fermer l'application proprement
        // On lui met un fond rouge opaque avec un texte rouge clair pour qu'il soit bien visible
        ImGui::SetCursorPosX(ImGui::GetWindowWidth() - 80);
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(1.00f, 0.00f, 0.00f, 0.2f));
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.00f, 0.00f, 0.00f, 1.00f));
        if (ImGui::Button("Quitter")) {

        }
        ImGui::PopStyleColor(2);

        ImGui::EndMainMenuBar();

    }
}

// Command Bar — barre fixée tout en haut (exchange, modèle, symbole)
void ControlPanel::render_command_bar() {
    ImGuiViewport* viewport = ImGui::GetMainViewport();

    float command_bar_height = ImGui::GetFrameHeight() + 8.0f;
    // Ancre la barre en bas de la zone de travail : coin bas-gauche décalé vers le haut
    // de sa propre hauteur.
    ImVec2 command_bar_pos(viewport->WorkPos.x,
                           viewport->WorkPos.y + viewport->WorkSize.y - command_bar_height);
    ImGui::SetNextWindowPos(command_bar_pos);
    ImGui::SetNextWindowSize(ImVec2(viewport->WorkSize.x, command_bar_height));
    // Multi-viewport activé : ancre la barre dans le viewport principal pour l'empêcher
    // de se détacher dans sa propre fenêtre OS et de perdre son positionnement.
    ImGui::SetNextWindowViewport(viewport->ID);

    ImGuiWindowFlags window_flags = ImGuiWindowFlags_NoDecoration |
                                    ImGuiWindowFlags_NoMove |
                                    ImGuiWindowFlags_NoResize |
                                    ImGuiWindowFlags_NoSavedSettings |
                                    ImGuiWindowFlags_NoScrollbar |
                                    ImGuiWindowFlags_NoDocking;

    ImGui::Begin("Command Bar", NULL, window_flags);

    static char command_bar[32] = "";

    ImGui::InputText("##Symbols", command_bar, IM_ARRAYSIZE(command_bar));

    ImGui::End();
}

// ---------------------------------------------------------------------------
// Log Panel — fenêtre de logs textuels avec filtre et auto-scroll
// ---------------------------------------------------------------------------

/// Structure interne maintenant un buffer de texte et les offsets de lignes pour
/// un accès O(1) à chaque ligne (nécessaire pour ImGuiListClipper).
struct ExampleAppLog
{
    ImGuiTextBuffer     Buf;
    ImGuiTextFilter     Filter;
    ImVector<int>       LineOffsets; ///< Offset de début de chaque ligne dans Buf
    bool                AutoScroll;  ///< Si true, force le scroll en bas à chaque frame

    ExampleAppLog()
    {
        AutoScroll = true;
        Clear();
    }

    void Clear()
    {
        Buf.clear();
        LineOffsets.clear();
        LineOffsets.push_back(0); // La première ligne commence à l'offset 0
    }

    void AddLog(const char* fmt, ...) IM_FMTARGS(2)
    {
        int old_size = Buf.size();
        va_list args;
        va_start(args, fmt);
        Buf.appendfv(fmt, args);
        va_end(args);
        // Met à jour l'index des offsets de lignes pour chaque '\n' ajouté
        for (int new_size = Buf.size(); old_size < new_size; old_size++)
            if (Buf[old_size] == '\n')
                LineOffsets.push_back(old_size + 1);
    }

    /// @brief 
    /// @param title 
    /// @param p_open 
    void Draw(const char* title, bool* p_open = NULL)
    {
        if (!ImGui::Begin(title, p_open))
        {
            ImGui::End();
            return;
        }

        if (ImGui::BeginPopup("Options"))
        {
            ImGui::Checkbox("Auto-scroll", &AutoScroll);
            ImGui::EndPopup();
        }

        if (ImGui::Button("Options"))
            ImGui::OpenPopup("Options");
        ImGui::SameLine();
        bool clear = ImGui::Button("Clear");
        ImGui::SameLine();
        bool copy = ImGui::Button("Copy");
        ImGui::SameLine();
        Filter.Draw("Filter", -100.0f);

        ImGui::Separator();

        if (ImGui::BeginChild("scrolling", ImVec2(0, 0), ImGuiChildFlags_None, ImGuiWindowFlags_HorizontalScrollbar))
        {
            if (clear) Clear();
            if (copy)  ImGui::LogToClipboard();

            ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 0));
            const char* buf     = Buf.begin();
            const char* buf_end = Buf.end();

            if (Filter.IsActive())
            {
                // Avec un filtre actif on ne peut pas utiliser le clipper car on n'a pas
                // d'accès aléatoire aux lignes visibles — on parcourt tout le buffer
                for (int line_no = 0; line_no < LineOffsets.Size; line_no++)
                {
                    const char* line_start = buf + LineOffsets[line_no];
                    const char* line_end   = (line_no + 1 < LineOffsets.Size)
                                             ? (buf + LineOffsets[line_no + 1] - 1)
                                             : buf_end;
                    if (Filter.PassFilter(line_start, line_end))
                        ImGui::TextUnformatted(line_start, line_end);
                }
            }
            else
            {
                // Sans filtre, le clipper ne rend que les lignes visibles à l'écran
                // ce qui évite de trop consommer le CPU sur de gros volumes de logs
                ImGuiListClipper clipper;
                clipper.Begin(LineOffsets.Size);
                while (clipper.Step())
                {
                    for (int line_no = clipper.DisplayStart; line_no < clipper.DisplayEnd; line_no++)
                    {
                        const char* line_start = buf + LineOffsets[line_no];
                        const char* line_end   = (line_no + 1 < LineOffsets.Size)
                                                 ? (buf + LineOffsets[line_no + 1] - 1)
                                                 : buf_end;
                        ImGui::TextUnformatted(line_start, line_end);
                    }
                }
                clipper.End();
            }
            ImGui::PopStyleVar();

            // Auto-scroll : ne force le bas que si l'utilisateur était déjà en bas
            // (permet de scroller vers le haut sans être ramené automatiquement)
            if (AutoScroll && ImGui::GetScrollY() >= ImGui::GetScrollMaxY())
                ImGui::SetScrollHereY(1.0f);
        }
        ImGui::EndChild();
        ImGui::End();
    }
};

void LogPanel::render() {
    static ExampleAppLog log; // Instance statique unique — persiste entre les frames
    log.Draw("Example: Log");
}

// ---------------------------------------------------------------------------
// Control Panel — contrôles Hawkes : workers, calibration, entraînement, symboles
// ---------------------------------------------------------------------------
void ControlPanel::render_control_panel() {
    ImGui::Begin("Control Panel");

    static ImGuiTreeNodeFlags base_flags = ImGuiTreeNodeFlags_DrawLinesFull | ImGuiTreeNodeFlags_DefaultOpen;
    ImGui::CheckboxFlags("ImGuiTreeNodeFlags_DrawLinesNone",    &base_flags, ImGuiTreeNodeFlags_DrawLinesNone);
    ImGui::CheckboxFlags("ImGuiTreeNodeFlags_DrawLinesFull",    &base_flags, ImGuiTreeNodeFlags_DrawLinesFull);
    ImGui::CheckboxFlags("ImGuiTreeNodeFlags_DrawLinesToNodes", &base_flags, ImGuiTreeNodeFlags_DrawLinesToNodes);

    if (ImGui::TreeNodeEx("Hawkes Model", base_flags)) {

        // --- Performance ---
        if (ImGui::TreeNodeEx("Performance Controls", base_flags)) {
            static int n_workers = DefaultParameters::N_MAX_WORKERS;
            ImGui::SliderInt("Number of Workers",        &n_workers,          1, 10);
            ImGui::SliderInt("Update Speed (per seconds)", &this->update_speed, 1, 200);
            ImGui::TreePop();
        }

        // --- Calibration ---
        if (ImGui::TreeNodeEx("Calibration Controls", base_flags)) {
            ImGui::Text("Select Exchanges to Connect:");
            ImGui::Spacing();

            // Boutons toggle colorés : vert = connecté, gris/rouge = déconnecté
            static std::vector<std::string> exchanges = {"Binance", "Coinbase", "Kraken", "Bybit", "OKX"};
            static bool states[5] = {true, true, true, false, false};

            const ImVec2 button_size = ImVec2(80, 40);

            for (int i = 0; i < (int)exchanges.size(); i++) {
                if (i > 0) ImGui::SameLine();
                ImGui::PushID(i);

                if (states[i]) {
                    ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4(0.0f, 0.6f, 0.0f, 1.0f));
                    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.0f, 0.8f, 0.0f, 1.0f));
                    ImGui::PushStyleColor(ImGuiCol_ButtonActive,  ImVec4(0.0f, 0.4f, 0.0f, 1.0f));
                } else {
                    ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4(0.4f, 0.4f, 0.4f, 1.0f));
                    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.6f, 0.2f, 0.2f, 1.0f));
                    ImGui::PushStyleColor(ImGuiCol_ButtonActive,  ImVec4(0.8f, 0.1f, 0.1f, 1.0f));
                }

                if (ImGui::Button(exchanges[i].c_str(), button_size)) {
                    states[i] = !states[i];
                    // TODO: propager l'état au SchedulerConfig
                    // config.websocket_map[exchanges[i]].active = states[i];
                }

                ImGui::PopStyleColor(3); // Toujours dépiler le même nombre que PushStyleColor
                ImGui::PopID();
            }

            static int calibration_duration = DefaultParameters::CALIBRATION_TIME;
            ImGui::InputInt("Calibration Duration (s)", &calibration_duration);
            ImGui::TreePop();
        }

        // --- Entraînement ---
        if (ImGui::TreeNodeEx("Training Controls", base_flags)) {
            static int training_duration = DefaultParameters::TRAINING_TIME;
            ImGui::InputInt("Training Duration (s)", &training_duration);
            ImGui::TreePop();
        }

        this->render_symbol_selector();
        ImGui::TreePop();
    }

    ImGui::Spacing();
    ImGui::Separator();

    if (ImGui::Button("START CALIBRATION", ImVec2(-1, 40))) {
        // TODO: lancer la calibration
    }

    ImGui::End();
}

// Tableau de métriques — une ligne par source (exchange), avec la qualité du modèle
// en temps réel lue depuis le TelemetryManager. Pour l'instant seule l'EWMA des
// résidus est réellement alimentée ; les autres colonnes (Chi^2, W, K, res/s., prix,
// variation) sont des emplacements réservés en attendant que la télémétrie les expose.
void ControlPanel::render_metrics_table() {
    ImGui::Begin("Metrics Panel");

    std::vector<std::string> members = config.websocket_map.getMemberNames();
    int n = (int)members.size();

    ImGuiTableFlags flags = ImGuiTableFlags_RowBg |
                            ImGuiTableFlags_SizingFixedFit |
                            ImGuiTableFlags_BordersH |
                            ImGuiTableFlags_SizingStretchSame;

    int number_metrics_column = 9;
    #ifdef STRESS_TEST
        number_metrics_column = 7;
    #endif

    // Snapshot des buffers de résidus lu UNE seule fois par frame (copie sous verrou
    // partagé). Chaque source possède son propre buffer d'EWMA ; l'index de source
    // suit l'ordre des membres de websocket_map, donc `row` indexe directement le buffer.
    auto residual_snap = telemetry_manager.get_residualsBuffers_snapshot(this->current_symbol_index);
    const auto& ewma_buffers = residual_snap.ewmaBuffers_by_sources;

    auto chi2_snap = telemetry_manager.get_chi2metrics_snapshot(this->current_symbol_index);

    // Taux d'événements reçus (events/s) par source, publié en continu par le worker.
    auto event_rates = telemetry_manager.get_event_rates_snapshot(this->current_symbol_index);

    // Bande de tolérance : l'EWMA des résidus d'un modèle bien calibré (~Exp(1)) oscille
    // autour de 1. On tolère ±20 % avant de signaler une dérive (cohérent avec render_residuals_ewma).
    const double band_lo = 0.5, band_hi = 1.5;
    const ImVec4 col_ok    = ImVec4(0.30f, 0.80f, 0.31f, 1.0f); // dans la bande
    const ImVec4 col_drift = ImVec4(0.89f, 0.29f, 0.29f, 1.0f); // hors bande

    if (ImGui::BeginTable("table", number_metrics_column, flags)) {

        ImGui::TableSetupColumn("Membres");
        ImGui::TableSetupColumn("EWMA");
        ImGui::TableSetupColumn("Chi^2");
        ImGui::TableSetupColumn("Critical"); // seuil critique/limite pour l'acceptation de H0
        ImGui::TableSetupColumn("W");
        ImGui::TableSetupColumn("K");
        // ImGui::TableSetupColumn("Score");
        ImGui::TableSetupColumn("event/s.");
        #ifndef STRESS_TEST
            ImGui::TableSetupColumn("Price");
            ImGui::TableSetupColumn("Var.(%)");
        #endif
        // ImGui::TableSetupColumn("Exch.Status");

        ImGui::TableHeadersRow();

        for (int row = 0; row < n; ++row) {
            ImGui::TableNextRow(); // On crée une nouvelle ligne

            // --- Membre (source) ---
            ImGui::TableNextColumn();
            ImGui::Text("%s", members[row].c_str());

            // --- EWMA : valeur courante des résidus de la source ---
            // On lit la dernière valeur via get_ordered_items().back() : get() consommerait
            // le plus ancien élément (mauvaise valeur) au lieu de renvoyer la plus récente.
            ImGui::TableNextColumn();
            if (row < (int)ewma_buffers.size() && !ewma_buffers[row].empty()) {
                double ewma = ewma_buffers[row].get_ordered_items().back();
                bool in_band = (ewma >= band_lo && ewma <= band_hi);
                ImGui::TextColored(in_band ? col_ok : col_drift, "%.3f", ewma);
            } else {
                ImGui::TextDisabled("--");
            }

            ImGui::TableNextColumn();
            if (row < (int)chi2_snap.chi2metrics_by_sources.size()) {
                double chi2 = chi2_snap.chi2metrics_by_sources[row].chi2_statistic;
                bool is_accepted = chi2 >= 0.0 && chi2 <= chi2_snap.chi2metrics_by_sources[row].chi2_critical_value;
                ImGui::TextColored(is_accepted ? col_ok : col_drift, "%.3f", chi2);
            } else {
                ImGui::TextDisabled("--");
            }

            ImGui::TableNextColumn();
            if (row < (int)chi2_snap.chi2metrics_by_sources.size()) {
                double critical_value = chi2_snap.chi2metrics_by_sources[row].chi2_critical_value;
                ImGui::Text("%.3f", critical_value);
            } else {
                ImGui::TextDisabled("--");
            }

            ImGui::TableNextColumn();
            if (row < (int)chi2_snap.chi2metrics_by_sources.size()) {
                int W = chi2_snap.W[row];
                ImGui::Text("%d", W);
            } else {
                ImGui::TextDisabled("--");
            }

            ImGui::TableNextColumn();
            if (row < (int)chi2_snap.chi2metrics_by_sources.size()) {
                double K = chi2_snap.chi2metrics_by_sources[row].K;
                ImGui::Text("%.0f", K);
            } else {
                ImGui::TextDisabled("--");
            }

            // --- event/s. : taux d'événements reçus pour cette source ---
            ImGui::TableNextColumn();
            if (row < (int)event_rates.size()) {
                ImGui::Text("%.1f", event_rates[row]);
            } else {
                ImGui::TextDisabled("--");
            }

            // --- Colonnes non encore alimentées par la télémétrie ---
            for (int col = 0; col < number_metrics_column - 7; col++) {
                ImGui::TableNextColumn();
                ImGui::TextDisabled("--");
            }
        }
        ImGui::EndTable();
    }
    ImGui::End();
}

// Point d'entrée polymorphe du panneau de contrôle (appelé par UI_renderer).
void ControlPanel::render() {
    render_main_bar();
    render_command_bar();
    render_control_panel();
    // render_residuals_ewma();
    render_metrics_table();
}

// EWMA des résidus — métrique de qualité du modèle en temps réel.
// Le compensateur intégré entre deux événements consécutifs d'une même source suit,
// pour un modèle bien calibré, une loi Exp(1) (moyenne 1). L'EWMA de ces résidus doit
// donc osciller autour de 1 : une dérive durable au-dessus signale un modèle qui
// sous-estime l'intensité, en dessous un modèle qui la surestime.
void ControlPanel::render_residuals_ewma() {
    ImGui::Begin("EWMA Résidus");

    auto snap = telemetry_manager.get_residualsBuffers_snapshot(this->current_symbol_index);
    int n_sources = (int)snap.ewmaBuffers_by_sources.size();

    int max_len = 0;
    for (const auto& v : snap.ewmaBuffers_by_sources) max_len = std::max(max_len, (int)v.size());

    if (max_len == 0) {
        ImGui::TextDisabled("En attente de résidus calibrés...");
        ImGui::End();
        return;
    }

    // Lecture rapide de la valeur courante de la métrique pour chaque source
    for (int src = 0; src < n_sources; src++) {
        const auto ewma = snap.ewmaBuffers_by_sources[src].get_ordered_items();
        if (ewma.empty()) continue;
        const char* ws_name = (src + 1 < (int)DefaultParameters::websockets.size())
                                  ? DefaultParameters::websockets[src + 1].c_str() : "?";
        if (src > 0) ImGui::SameLine();
        ImGui::TextColored(ImPlot::GetColormapColor(src), "%s: %.3f", ws_name, ewma.back());
    }
    ImGui::Separator();

    if (ImPlot::BeginPlot("##EWMAResiduals", ImVec2(-1, -1))) {
        ImPlot::SetupAxes("Résidus récents (fenêtre glissante)", "EWMA des résidus",
                          ImPlotAxisFlags_AutoFit, ImPlotAxisFlags_AutoFit);

        const double x_max = (double)(max_len - 1);

        // Bande de tolérance : zone où l'EWMA reste statistiquement acceptable autour de 1.
        // Les résidus d'un modèle bien calibré suivent Exp(1) (moyenne 1) ; on tolère ±20 %
        // avant de considérer une dérive. La bande sert de repère visuel immédiat.
        const double band_lo = 0.8, band_hi = 1.2;
        double band_xs[2]    = {0.0, x_max};
        double band_lo_ys[2] = {band_lo, band_lo};
        double band_hi_ys[2] = {band_hi, band_hi};
        ImPlot::SetNextFillStyle(ImVec4(0.3f, 0.8f, 0.3f, 1.0f), 0.10f);
        ImPlot::PlotShaded("Zone acceptable", band_xs, band_lo_ys, band_hi_ys, 2);

        // Cible théorique : moyenne des résidus Exp(1) = 1 (modèle parfaitement calibré)
        double ref_ys[2] = {1.0, 1.0};
        ImPlot::SetNextLineStyle(ImVec4(0.9f, 0.3f, 0.3f, 1.0f), 1.5f);
        ImPlot::PlotLine("Cible = 1", band_xs, ref_ys, 2);

        // Une courbe par source, tracée dans l'ordre chronologique : fenêtre glissante
        // continue, sans reset ni discontinuité au point d'enroulement du ring.
        // Couleur cohérente avec le QQ plot et les intensités.
        for (int src = 0; src < n_sources; src++) {
            const auto ewma = snap.ewmaBuffers_by_sources[src].get_ordered_items();
            if (ewma.empty()) continue;
            const char* ws_name = (src + 1 < (int)DefaultParameters::websockets.size())
                                      ? DefaultParameters::websockets[src + 1].c_str() : "?";
            ImPlot::SetNextLineStyle(ImPlot::GetColormapColor(src), 1.6f);
            ImPlot::PlotLine(ws_name, ewma.data(), (int)ewma.size());
        }

        ImPlot::EndPlot();
    }

    ImGui::End();
}


// ---------------------------------------------------------------------------
// Title Bar — barre fixe sous la selector bar avec horloge temps réel
// ---------------------------------------------------------------------------
void UserInterface::render_title_bar() {
    ImGuiViewport* viewport = ImGui::GetMainViewport();

    // Positionne la title bar juste en dessous de la selector bar
    float selector_bar_height = ImGui::GetFrameHeight() + 8.0f;
    ImGui::SetNextWindowPos(ImVec2(viewport->WorkPos.x, viewport->WorkPos.y + selector_bar_height));

    float title_bar_height = ImGui::GetFrameHeight() + 8.0f;
    ImGui::SetNextWindowSize(ImVec2(viewport->WorkSize.x, title_bar_height));

    ImGuiWindowFlags window_flags = ImGuiWindowFlags_NoDecoration |
                                    ImGuiWindowFlags_NoMove |
                                    ImGuiWindowFlags_NoResize |
                                    ImGuiWindowFlags_NoSavedSettings |
                                    ImGuiWindowFlags_NoScrollbar |
                                    ImGuiWindowFlags_NoDocking;

    ImGui::Begin("Title Bar", NULL, window_flags);

    ImGui::Text("Hawkes Models Dashboard");
    ImGui::SameLine();
    ImGui::TextDisabled(" - Real-time Monitoring and Control");

    // Affiche l'heure locale alignée à droite
    auto t   = std::time(nullptr);
    auto tm  = *std::localtime(&t);
    std::ostringstream oss;
    oss << std::put_time(&tm, "%Y-%m-%d %H:%M:%S");
    std::string time_str = oss.str();

    float time_text_width = ImGui::CalcTextSize(time_str.c_str()).x;
    float padding_right   = ImGui::GetStyle().WindowPadding.x;

    ImGui::SameLine(ImGui::GetWindowWidth() - time_text_width - padding_right);
    ImGui::Text("%s", time_str.c_str());

    ImGui::End();
}

// ---------------------------------------------------------------------------
// Symbol Selector — arborescence de sélection multiple des symboles
// ---------------------------------------------------------------------------
void ControlPanel::render_symbol_selector() {
    if (ImGui::TreeNode("Symbols"))
    {
        for (int i = 0; i < (int)config.symbols.size(); i++) {
            // Selectable avec état persistant dans is_symbol_selected
            if (ImGui::Selectable(config.symbols[i].c_str(), DefaultParameters::is_symbol_selected[i])) {
                DefaultParameters::is_symbol_selected[i] = !DefaultParameters::is_symbol_selected[i];
            }
        }
        ImGui::TreePop();
    }
}

// ######################################################################
// PANEL ANALYTICS - PANEL ANALYTICS - PANEL ANALYTICS
// ######################################################################


// Met à jour les buffers : un point par exchange, pour chaque symbole sélectionné.
// À appeler UNE fois par frame, AVANT le rendu des graphiques.
void AnalyticsPanel::update_intensity_buffers() {
    float t_render = (float)ImGui::GetTime();

    // Rescan complet toutes les ~120 frames pour corriger le max quand l'ancien maximum
    // sort de la fenêtre visible — le reste du temps la mise à jour est O(1).
    bool do_rescan = (++rescan_frame_counter >= 120);
    if (do_rescan) rescan_frame_counter = 0;
    float t_min_visible = t_render - intensity_history;

    for (int i = 0; i < (int)config.symbols.size(); i++) {
        if (!DefaultParameters::is_symbol_selected[i])
            continue;

        auto snap = telemetry_manager.get_snapshot(i);
        if (snap.intensities.empty())
            continue;

        const std::string& sym = config.symbols[i];
        auto& bufs   = all_buffers[sym];
        auto& maxes  = all_buf_max[sym];

        if (bufs.size() < snap.intensities.size())
            bufs.resize(snap.intensities.size());
        if (maxes.size() < snap.intensities.size())
            maxes.resize(snap.intensities.size(), 0.0f);

        for (int e = 0; e < (int)snap.intensities.size(); e++) {
            float val = (float)snap.intensities[e];
            bufs[e].AddPoint(t_render, val);

            if (val >= maxes[e]) {
                maxes[e] = val;  // O(1) : nouveau max immédiat
            } else if (do_rescan) {
                // O(n) mais ~2× par seconde seulement : recalcule le vrai max visible
                float true_max = 0.0f;
                for (const auto& p : bufs[e].Data)
                    if (p.x >= t_min_visible) true_max = std::max(true_max, p.y);
                maxes[e] = true_max;
            }
        }
    }
}

// Graphique compact d'un seul exchange (une "ligne" du panneau de gauche).
void AnalyticsPanel::render_exchange_strip(int source_idx) {
    const ImVec4 col = ImPlot::GetColormapColor(source_idx);
    const char* ws_name = DefaultParameters::websockets[source_idx + 1].c_str();

    // En-tête : pastille colorée + nom + dernière valeur
    ImGui::PushStyleColor(ImGuiCol_Text, col);
    ImGui::Text("%s", ws_name);
    ImGui::PopStyleColor();

    float t_now = (float)ImGui::GetTime();
    std::string plot_id = "##strip_" + std::to_string(source_idx);

    // Calcule le max visible sur tous les symboles sélectionnés pour cet exchange — O(1)
    float auto_y_max = 0.0f;
    for (int i = 0; i < (int)config.symbols.size(); i++) {
        if (!DefaultParameters::is_symbol_selected[i]) continue;
        auto it = all_buf_max.find(config.symbols[i]);
        if (it == all_buf_max.end()) continue;
        if (source_idx < (int)it->second.size())
            auto_y_max = std::max(auto_y_max, it->second[source_idx]);
    }
    auto_y_max = std::max(auto_y_max * 1.1f, 0.1f);  // +10 %, plancher à 0.1

    // Graphique compact : hauteur réduite, pas de légende, axes minimaux
    if (ImPlot::BeginPlot(plot_id.c_str(), ImVec2(-1, 90),
                          ImPlotFlags_NoLegend | ImPlotFlags_NoTitle | ImPlotFlags_NoMouseText)) {
        ImPlot::SetupAxes(nullptr, nullptr,
                          ImPlotAxisFlags_NoTickLabels | ImPlotAxisFlags_NoGridLines,
                          ImPlotAxisFlags_NoGridLines);
        ImPlot::SetupAxisLimits(ImAxis_X1, t_now - intensity_history, t_now, ImGuiCond_Always);
        ImPlot::SetupAxisLimits(ImAxis_Y1, 0, auto_y_max, ImGuiCond_Always);

        for (int i = 0; i < (int)config.symbols.size(); i++) {
            if (!DefaultParameters::is_symbol_selected[i]) continue;
            const std::string& sym = config.symbols[i];
            auto& bufs = all_buffers[sym];
            if (source_idx >= (int)bufs.size()) continue;
            auto& buf = bufs[source_idx];
            if (buf.Data.empty()) continue;

            std::string label = sym + " [" + ws_name + "]";
            ImPlot::SetNextLineStyle(col, 1.4f);
            ImPlot::SetNextFillStyle(col, 0.15f);
            ImPlot::PlotLine(label.c_str(),
                             &buf.Data[0].x, &buf.Data[0].y,
                             (int)buf.Data.size(),
                             ImPlotLineFlags_None, buf.Offset, 2 * sizeof(float));
        }
        ImPlot::EndPlot();
    }
}

// Panneau "intensités" — empile une bande compacte par exchange.
void AnalyticsPanel::render_intensities_plot() {
    update_intensity_buffers();  // mise à jour des buffers une seule fois

    ImGui::Begin("Hawkes Intensities");

    // Contrôle de la fenêtre temporelle visible (partagé avec update_intensity_buffers)
    ImGui::SetNextItemWidth(130);
    ImGui::SliderFloat("History", &intensity_history, 1, 60, "%.0f s");
    ImGui::Separator();

    int n_exchanges = (int)DefaultParameters::websockets.size() - 1;
    for (int source_idx = 0; source_idx < n_exchanges; source_idx++) {
        render_exchange_strip(source_idx);
        ImGui::Spacing();
    }

    ImGui::End();
}

// QQ Plot — résidus du compensateur de Hawkes vs Exp(1) théorique
void AnalyticsPanel::render_qq_plot() {
    ImGui::Begin("QQ Plot — Résidus Hawkes");

    auto snap = telemetry_manager.get_residualsBuffers_snapshot(this->current_symbol_index);
    int n_sources = (int)snap.residualBuffers_by_source.size();

    // Compte total de résidus pour l'affichage
    int total_residuals = 0;
    for (auto& v : snap.residualBuffers_by_source) total_residuals += (int)v.size();
    ImGui::SameLine();
    ImGui::TextDisabled("(%d résidus)", total_residuals);

    if (n_sources == 0) {
        ImGui::TextDisabled("En attente de résidus calibrés...");
        ImGui::End();
        return;
    }

    // Calcule les QQ points par source et le max global pour la droite de référence
    static const int N_MAX = 2000;
    struct SourceQQ { std::vector<float> xs, ys; };
    std::vector<SourceQQ> per_source(n_sources);
    float axis_max = 0.0f;

    for (int src = 0; src < n_sources; src++) {
        const auto& raw = snap.residualBuffers_by_source[src].get_all_items();
        if (raw.size() < 2) continue;

        std::vector<double> sample;
        if ((int)raw.size() > N_MAX)
            sample.assign(raw.end() - N_MAX, raw.end());
        else
            sample = raw;

        int m = (int)sample.size();
        std::sort(sample.begin(), sample.end());

        // Quantiles théoriques Exp(1) : Q(p) = -ln(1-p), avec p_i = (i+0.5)/m (formule de Hazen)
        per_source[src].xs.resize(m);
        per_source[src].ys.resize(m);
        for (int i = 0; i < m; i++) {
            double p = (i + 0.5) / (double)m;
            per_source[src].xs[i] = (float)(-std::log(1.0 - p));
            per_source[src].ys[i] = (float)sample[i];
        }
        axis_max = std::max(axis_max,
                            std::max(per_source[src].xs[m - 1], per_source[src].ys[m - 1]));
    }

    if (axis_max == 0.0f) {
        ImGui::TextDisabled("En attente de résidus calibrés...");
        ImGui::End();
        return;
    }

    axis_max *= 1.1f;
    float ref_xs[2] = {0.0f, axis_max};
    float ref_ys[2] = {0.0f, axis_max};

    if (ImPlot::BeginPlot("##QQPlot", ImVec2(-1, -1))) {
        ImPlot::SetupAxes("Quantiles théoriques Exp(1)", "Quantiles empiriques",
                          ImPlotAxisFlags_AutoFit, ImPlotAxisFlags_AutoFit);

        // Droite de référence y = x (modèle parfait)
        ImPlot::SetNextLineStyle(ImVec4(0.9f, 0.3f, 0.3f, 1.0f), 1.5f);
        ImPlot::PlotLine("Exp(1) théorique", ref_xs, ref_ys, 2);

        // Une série par source avec la couleur du colormap ImPlot (cohérent avec le graphique des intensités)
        for (int src = 0; src < n_sources; src++) {
            auto& qq = per_source[src];
            if (qq.xs.empty()) continue;
            // websockets[0] = "All", donc source 0 → websockets[1]
            const char* ws_name = DefaultParameters::websockets[src + 1].c_str();
            ImPlot::SetNextMarkerStyle(ImPlotMarker_Circle, 3.0f,
                                       ImPlot::GetColormapColor(src), 0.7f,
                                       ImPlot::GetColormapColor(src));
            ImPlot::PlotScatter(ws_name, qq.xs.data(), qq.ys.data(), (int)qq.xs.size());
        }

        ImPlot::EndPlot();
    }

    ImGui::End();
}

void AnalyticsPanel::render_branching_matrix(std::vector<double> branching_matrix) {
    ImGui::Begin("Branching Matrix");

    std::vector<std::string> members = config.websocket_map.getMemberNames();
    int n = (int)members.size();

    if (branching_matrix.size() < (size_t)(n * n)) {
        ImGui::TextDisabled("En attente des paramètres calibrés...");
        ImGui::End();
        return;
    }

    if (ImGui::BeginTable("BranchingMatrix", n + 1)) {
        ImGui::TableSetupColumn("");
        for (int col = 0; col < n; ++col)
            ImGui::TableSetupColumn(members[col].c_str());
        ImGui::TableHeadersRow();

        for (int row = 0; row < n; ++row) {
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImU32 header_bg_color = ImGui::GetColorU32(ImGuiCol_TableHeaderBg);
            ImGui::TableSetBgColor(ImGuiTableBgTarget_CellBg, header_bg_color);
            ImGui::Text("%s", members[row].c_str());

            for (int col = 0; col < n; ++col) {
                ImGui::TableNextColumn();
                double value = branching_matrix[row * n + col];
                // Plus la valeur est proche de 1, plus la couleur du fond est rouge de manière exponentielle; plus elle est proche de 0, plus elle est noire
                value = pow(value, branching_matrix_color_power_coeff); // Ajuste la sensibilité de la couleur
                ImU32 cell_bg_color = ImGui::GetColorU32(ImVec4((float)value, 0.0f, 0.0f, 1.0f));
                ImGui::TableSetBgColor(ImGuiTableBgTarget_CellBg, cell_bg_color);
                ImGui::Text("%.3f", branching_matrix[row * n + col]);
            }
        }
        ImGui::EndTable();
    }
    ImGui::End();
}

void AnalyticsPanel::render() {
    std::vector<double> branching_matrix = telemetry_manager.get_parameters_snapshot(this->current_symbol_index).branching_matrix;
    this->render_branching_matrix(branching_matrix);
    this->render_intensities_plot();
    this->render_qq_plot();
    #ifdef STRESS_TEST
    const auto& params = DefaultParameters::all_OgataParams[this->current_symbol_index];
    std::vector<double> theo_branching_matrix(
        params.branching_matrix,
        params.branching_matrix + params.D * params.D
    );
    this->render_branching_matrix(theo_branching_matrix);
    #endif
}

// void UserInterface::render_parameters_panel() {
//     ImGui::Begin("Parameters Panel");
//     opt_hawkesParams params = telemetry_manager.get_parameters_snapshot(this->current_symbol_index).params;
    
//     std::vector<std::string> members = config.websocket_map.getMemberNames();
//     int n = (int)members.size();

//     static const int NUM_PARAM = 3; // mu, alpha, beta

//     if (ImGui::BeginTable("ParametersTable", NUM_PARAM + 1)) {
//         ImGui::TableSetupColumn("");
//         ImGui::TableSetupColumn("base intensity");
//         ImGui::TableSetupColumn("alpha");
//         ImGui::TableSetupColumn("beta");
//         ImGui::TableHeadersRow();

//         for (int row = 0; row < n; ++row) {
//             ImGui::TableNextRow();
//             ImGui::TableNextColumn();
//             ImU32 header_bg_color = ImGui::GetColorU32(ImGuiCol_TableHeaderBg);
//             ImGui::TableSetBgColor(ImGuiTableBgTarget_CellBg, header_bg_color);
//             ImGui::Text("%s", members[row].c_str());

//             for (int col = 0; col < NUM_PARAM; ++col) {
//                 ImGui::TableNextColumn();
//                 double value = 0.0;
//                 if (col == 0) value = params.mu[row];
//                 else if (col == 1) value = params.alpha[row];
//                 else if (col == 2) value = params.beta[row];
//             }
//         }
//     }
// }

// ---------------------------------------------------------------------------
// Main Renderer — boucle principale ImGui + OpenGL avec gestion du framerate
// ---------------------------------------------------------------------------
int UserInterface::main_renderer() {
    initialize();

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImPlot::CreateContext();
    ImGuiIO& io = ImGui::GetIO(); (void)io;

    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;    // Fenêtres dockables entre elles
    io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;  // Fenêtres détachables hors de la fenêtre principale

    #if defined(__APPLE__)
    const char* glsl_version = "#version 150";
    #else
    const char* glsl_version = "#version 130";
    #endif

    ApplyBloombergStyle();
    ApplyPlotStyle();
    LoadTerminalFont();

    ImGui_ImplGlfw_InitForOpenGL(this->window, true);
    ImGui_ImplOpenGL3_Init(glsl_version);

    ImVec4 clear_color = ImGui::GetStyle().Colors[ImGuiCol_WindowBg];

    bool show_demo_window = false;

    double rate      = 1.0 / DefaultParameters::UPDATE_SPEED;
    double last_time = glfwGetTime();

    while (true) {
        if (glfwWindowShouldClose(this->window)) break;

        glfwPollEvents();

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        // DockSpace global — doit être le premier widget rendu chaque frame
        ImGui::DockSpaceOverViewport(0, ImGui::GetMainViewport());

        if (show_demo_window) ImGui::ShowDemoWindow(&show_demo_window);

        UI_renderer();  // Rendu des panels personnalisés

        // Rendu OpenGL
        ImGui::Render();
        int display_w, display_h;
        glfwGetFramebufferSize(window, &display_w, &display_h);
        glViewport(0, 0, display_w, display_h);
        glClearColor(clear_color.x, clear_color.y, clear_color.z, clear_color.w);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

        // Mise à jour des viewports détachés (multi-fenêtres)
        if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable)
        {
            GLFWwindow* backup_current_context = glfwGetCurrentContext();
            ImGui::UpdatePlatformWindows();
            ImGui::RenderPlatformWindowsDefault();
            glfwMakeContextCurrent(backup_current_context);
        }

        glfwSwapBuffers(window);

        // --- Gestion précise du framerate ---
        // Stratégie : sleep OS jusqu'à ~1 ms avant l'échéance, puis busy-wait
        // pour absorber l'imprécision du scheduler OS sans surcharger le CPU.
        rate = 1.0 / DefaultParameters::UPDATE_SPEED;
        double target_time  = last_time + rate;
        double current_time = glfwGetTime();

        if (current_time < target_time) {
            double sleep_time = target_time - current_time;
            if (sleep_time > 0.002) {
                std::this_thread::sleep_for(
                    std::chrono::milliseconds(static_cast<int>((sleep_time - 0.001) * 1000)));
            }
            while (glfwGetTime() < target_time) { /* spin */ }
        }

        // Avance last_time de manière stable (évite l'accumulation de drift)
        last_time += rate;
        // Reset si l'app a été gelée plus d'1 s (ex: resize, mise en veille)
        if (glfwGetTime() - last_time > 1.0) {
            last_time = glfwGetTime();
        }
    }

    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImPlot::DestroyContext();
    ImGui::DestroyContext();

    glfwDestroyWindow(this->window);
    glfwTerminate();

    return 0;
}

void UserInterface::UI_renderer() {
    // Par polymorphisme
    for (auto panel : this->panels) {
        panel->render();
    }
}

std::vector<UI_Panel*> create_default_panels_v2(SchedulerConfig& config, TelemetryManager& telemetry_manager) {
    std::vector<UI_Panel*> panels;
    panels.push_back(new ControlPanel(config, telemetry_manager));
    panels.push_back(new AnalyticsPanel(config, telemetry_manager));
    panels.push_back(new LogPanel(config, telemetry_manager));
    return panels;
}