#include "user_interface.h"

static void glfw_error_callback(int error, const char* description) {
    fprintf(stderr, "Glfw Error %d: %s\n", error, description);
}

UserInterface::UserInterface(SchedulerConfig& config, TelemetryManager& telemetry_manager)
    : config(config), telemetry_manager(telemetry_manager)
{
    // Pré-alloue un vecteur de buffers vide pour chaque symbole connu
    for (const std::string& symbol : config.symbols) {
        all_buffers[symbol] = std::vector<ScrollingBuffer>();
    }
    this->is_symbol_selected.resize(config.symbols.size(), false);
    // Active le symbole par défaut à l'ouverture
    this->is_symbol_selected[config.symbols_map[DefaultParameters::default_symbol].asInt()] = true;
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

// ---------------------------------------------------------------------------
// Selector Bar — barre fixée tout en haut (exchange, modèle, symbole)
// ---------------------------------------------------------------------------
void UserInterface::render_selector_bar() {
    ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->WorkPos);

    float selector_bar_height = ImGui::GetFrameHeight() + 8.0f;
    ImGui::SetNextWindowSize(ImVec2(viewport->WorkSize.x, selector_bar_height));

    ImGuiWindowFlags window_flags = ImGuiWindowFlags_NoDecoration |
                                    ImGuiWindowFlags_NoMove |
                                    ImGuiWindowFlags_NoResize |
                                    ImGuiWindowFlags_NoSavedSettings |
                                    ImGuiWindowFlags_NoScrollbar |
                                    ImGuiWindowFlags_NoDocking;

    ImGui::Begin("Selector Bar", NULL, window_flags);

    static int current_websocket = 0;
    static int current_model     = 0;
    static char symbol[32]       = "";

    // Répartit les 3 widgets sur toute la largeur disponible avec espacement uniforme
    float spacing       = ImGui::GetStyle().ItemSpacing.x;
    float available_width = ImGui::GetContentRegionAvail().x;
    float item_width    = (available_width - (spacing * 2.0f)) / 3.0f;

    ImGui::SetNextItemWidth(item_width);
    ImGui::Combo("##combo", &current_websocket, DefaultParameters::websockets, IM_ARRAYSIZE(DefaultParameters::websockets));

    ImGui::SameLine();
    ImGui::SetNextItemWidth(item_width);
    ImGui::Combo("##Models", &current_model, DefaultParameters::models, IM_ARRAYSIZE(DefaultParameters::models));

    ImGui::SameLine();
    ImGui::SetNextItemWidth(item_width);
    // CharsUppercase force la saisie en majuscules pour être cohérent avec les symboles de marché
    ImGui::InputText("##Symbols", symbol, IM_ARRAYSIZE(symbol), ImGuiInputTextFlags_CharsUppercase);

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

void UserInterface::render_log_panel() {
    static ExampleAppLog log; // Instance statique unique — persiste entre les frames
    log.Draw("Example: Log");
}

// ---------------------------------------------------------------------------
// Control Panel — contrôles Hawkes : workers, calibration, entraînement, symboles
// ---------------------------------------------------------------------------
void UserInterface::render_control_panel() {
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

void UserInterface::update_hawkes_models() {
    // TODO: appliquer les mises à jour de paramètres déclenchées depuis l'UI
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
void UserInterface::render_symbol_selector() {
    if (ImGui::TreeNode("Symbols"))
    {
        for (int i = 0; i < (int)config.symbols.size(); i++) {
            // Selectable avec état persistant dans is_symbol_selected
            if (ImGui::Selectable(config.symbols[i].c_str(), is_symbol_selected[i])) {
                is_symbol_selected[i] = !is_symbol_selected[i];
            }
        }
        ImGui::TreePop();
    }
}

// ---------------------------------------------------------------------------
// Scrolling Buffer — graphique défilant des intensités λ(t) en temps réel
// ---------------------------------------------------------------------------
void UserInterface::render_scrolling_buffer() {
    ImGui::Begin("Hawkes Intensities Real-Time");

    // Chaque frame : on récupère le dernier snapshot de chaque symbole sélectionné
    // et on ajoute un point par exchange dans le buffer correspondant
    float t_render = (float)ImGui::GetTime();
    for (int i = 0; i < (int)config.symbols.size(); i++) {
        if (!is_symbol_selected[i])
            continue;

        auto snap = telemetry_manager.get_snapshot(i);
        if (snap.intensities.empty())
            continue;

        const std::string& sym  = config.symbols[i];
        auto& bufs              = all_buffers[sym];
        // Crée les buffers manquants si un nouvel exchange apparaît dans le snapshot
        if (bufs.size() < snap.intensities.size())
            bufs.resize(snap.intensities.size());

        for (int k = 0; k < (int)snap.intensities.size(); k++)
            bufs[k].AddPoint(t_render, (float)snap.intensities[k]);
    }

    // Contrôles d'affichage
    static float history = 10.0f;
    ImGui::SetNextItemWidth(150);
    ImGui::SliderFloat("History Window", &history, 1, 60, "%.1f s");

    static float y_limit = 2.0f;
    ImGui::SameLine();
    ImGui::SetNextItemWidth(150);
    ImGui::DragFloat("Max Intensity Y", &y_limit, 0.1f, 0.1f, 100.0f);

    static ImPlotAxisFlags flags = ImPlotAxisFlags_None;
    float t_now = (float)ImGui::GetTime();

    if (ImPlot::BeginPlot("##ScrollingHawkes", ImVec2(-1, 400))) {
        ImPlot::SetupAxes("Time (s)", "λ (Intensity)", flags, flags);
        // Fenêtre X glissante : on force ImGuiCond_Always pour qu'elle suive le temps réel
        ImPlot::SetupAxisLimits(ImAxis_X1, t_now - history, t_now, ImGuiCond_Always);
        ImPlot::SetupAxisLimits(ImAxis_Y1, 0, y_limit);

        for (int i = 0; i < (int)config.symbols.size(); i++) {
            if (!is_symbol_selected[i]) continue;
            const std::string& sym = config.symbols[i];
            auto& bufs = all_buffers[sym];
            for (int k = 0; k < (int)bufs.size(); k++) {
                auto& buf = bufs[k];
                if (buf.Data.empty()) continue;
                // k+1 car websockets[0] = "All" (agrégé) ; k=0 correspond à websockets[1]
                const std::string& websocket = DefaultParameters::websockets[k + 1];
                std::string label = sym + " [" + websocket + "]";
                ImPlot::SetNextFillStyle(IMPLOT_AUTO_COL, 0.25f);
                ImPlot::PlotLine(label.c_str(),
                                 &buf.Data[0].x,
                                 &buf.Data[0].y,
                                 (int)buf.Data.size(),
                                 0, buf.Offset, 2 * sizeof(float));
            }
        }
        ImPlot::EndPlot();
    }
    ImGui::End();
}

// ---------------------------------------------------------------------------
// QQ Plot — diagnostic de calibration (données synthétiques provisoires)
// ---------------------------------------------------------------------------
void UserInterface::render_qq_plot() {
    ImGui::Begin("QQ Plot");

    // TODO: remplacer ces données synthétiques par les résidus du modèle Hawkes calibré
    srand(0);
    static float xs1[100], ys1[100];
    for (int i = 0; i < 100; ++i) {
        xs1[i] = i * 0.01f;
        ys1[i] = xs1[i] + 0.1f * ((float)rand() / (float)RAND_MAX);
    }
    static float xs2[50], ys2[50];
    for (int i = 0; i < 50; i++) {
        xs2[i] = 0.25f + 0.2f * ((float)rand() / (float)RAND_MAX);
        ys2[i] = 0.75f + 0.2f * ((float)rand() / (float)RAND_MAX);
    }

    if (ImPlot::BeginPlot("Scatter Plot")) {
        ImPlot::PlotScatter("Data 1", xs1, ys1, 100);
        ImPlot::PushStyleVar(ImPlotStyleVar_FillAlpha, 0.25f);
        ImPlot::SetNextMarkerStyle(ImPlotMarker_Square, 6,
                                   ImPlot::GetColormapColor(1), IMPLOT_AUTO,
                                   ImPlot::GetColormapColor(1));
        ImPlot::PlotScatter("Data 2", xs2, ys2, 50);
        ImPlot::PopStyleVar();
        ImPlot::EndPlot();
    }

    ImGui::End();
}

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

    ImGui_ImplGlfw_InitForOpenGL(this->window, true);
    ImGui_ImplOpenGL3_Init(glsl_version);

    ImVec4 clear_color = ImGui::GetStyle().Colors[ImGuiCol_WindowBg];

    bool show_demo_window = false;

    double rate      = 1.0 / this->update_speed;
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

        update_hawkes_models();
        render_log_panel();
        render_control_panel();
        render_scrolling_buffer();

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
        rate = 1.0 / this->update_speed;
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
