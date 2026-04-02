#include "user_interface.h"



static void glfw_error_callback(int error, const char* description) {
    fprintf(stderr, "Glfw Error %d: %s\n", error, description);
}

UserInterface::UserInterface(SchedulerConfig& config, TelemetryManager& telemetry_manager) : config(config), telemetry_manager(telemetry_manager) {
    for (const std::string& symbol : config.symbols) {
        all_buffers[symbol] = ScrollingBuffer();
    }
    this->is_symbol_selected.resize(config.symbols.size(), false);
    this->is_symbol_selected[config.symbols_map[DefaultParameters::default_symbol].asInt()] = true;
}

int UserInterface::initialize() {
    glfwSetErrorCallback(glfw_error_callback);
    if (!glfwInit()) return 1;

    const char* glsl_version = nullptr;

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
    glfwSwapInterval(1);

    // L'initialisation de l'interface graphique est terminée
    return 0;
}

void UserInterface::render_selector_bar() {
    ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->WorkPos); // Fixé tout en haut
    
    // Définir la hauteur de la Selector Bar
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
    static int current_model = 0;
    static char symbol[32] = "";

    float spacing = ImGui::GetStyle().ItemSpacing.x;
    float available_width = ImGui::GetContentRegionAvail().x;
    float item_width = (available_width - (spacing * 2.0f)) / 3.0f;

    ImGui::SetNextItemWidth(item_width);
    ImGui::Combo("##combo", &current_websocket, DefaultParameters::websockets, IM_ARRAYSIZE(DefaultParameters::websockets));

    ImGui::SameLine();
    ImGui::SetNextItemWidth(item_width);
    ImGui::Combo("##Models", &current_model, DefaultParameters::models, IM_ARRAYSIZE(DefaultParameters::models));

    ImGui::SameLine();
    ImGui::SetNextItemWidth(item_width);
    ImGui::InputText("##Symbols", symbol, IM_ARRAYSIZE(symbol), ImGuiInputTextFlags_CharsUppercase);

    ImGui::End();
}

struct ExampleAppLog
{
    ImGuiTextBuffer     Buf;
    ImGuiTextFilter     Filter;
    ImVector<int>       LineOffsets; // Index to lines offset. We maintain this with AddLog() calls.
    bool                AutoScroll;  // Keep scrolling if already at the bottom.

    ExampleAppLog()
    {
        AutoScroll = true;
        Clear();
    }

    void    Clear()
    {
        Buf.clear();
        LineOffsets.clear();
        LineOffsets.push_back(0);
    }

    void    AddLog(const char* fmt, ...) IM_FMTARGS(2)
    {
        int old_size = Buf.size();
        va_list args;
        va_start(args, fmt);
        Buf.appendfv(fmt, args);
        va_end(args);
        for (int new_size = Buf.size(); old_size < new_size; old_size++)
            if (Buf[old_size] == '\n')
                LineOffsets.push_back(old_size + 1);
    }

    void    Draw(const char* title, bool* p_open = NULL)
    {
        if (!ImGui::Begin(title, p_open))
        {
            ImGui::End();
            return;
        }

        // Options menu
        if (ImGui::BeginPopup("Options"))
        {
            ImGui::Checkbox("Auto-scroll", &AutoScroll);
            ImGui::EndPopup();
        }

        // Main window
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
            if (clear)
                Clear();
            if (copy)
                ImGui::LogToClipboard();

            ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 0));
            const char* buf = Buf.begin();
            const char* buf_end = Buf.end();
            if (Filter.IsActive())
            {
                // In this example we don't use the clipper when Filter is enabled.
                // This is because we don't have random access to the result of our filter.
                // A real application processing logs with ten of thousands of entries may want to store the result of
                // search/filter.. especially if the filtering function is not trivial (e.g. reg-exp).
                for (int line_no = 0; line_no < LineOffsets.Size; line_no++)
                {
                    const char* line_start = buf + LineOffsets[line_no];
                    const char* line_end = (line_no + 1 < LineOffsets.Size) ? (buf + LineOffsets[line_no + 1] - 1) : buf_end;
                    if (Filter.PassFilter(line_start, line_end))
                        ImGui::TextUnformatted(line_start, line_end);
                }
            }
            else
            {
                // The simplest and easy way to display the entire buffer:
                //   ImGui::TextUnformatted(buf_begin, buf_end);
                // And it'll just work. TextUnformatted() has specialization for large blob of text and will fast-forward
                // to skip non-visible lines. Here we instead demonstrate using the clipper to only process lines that are
                // within the visible area.
                // If you have tens of thousands of items and their processing cost is non-negligible, coarse clipping them
                // on your side is recommended. Using ImGuiListClipper requires
                // - A) random access into your data
                // - B) items all being the  same height,
                // both of which we can handle since we have an array pointing to the beginning of each line of text.
                // When using the filter (in the block of code above) we don't have random access into the data to display
                // anymore, which is why we don't use the clipper. Storing or skimming through the search result would make
                // it possible (and would be recommended if you want to search through tens of thousands of entries).
                ImGuiListClipper clipper;
                clipper.Begin(LineOffsets.Size);
                while (clipper.Step())
                {
                    for (int line_no = clipper.DisplayStart; line_no < clipper.DisplayEnd; line_no++)
                    {
                        const char* line_start = buf + LineOffsets[line_no];
                        const char* line_end = (line_no + 1 < LineOffsets.Size) ? (buf + LineOffsets[line_no + 1] - 1) : buf_end;
                        ImGui::TextUnformatted(line_start, line_end);
                    }
                }
                clipper.End();
            }
            ImGui::PopStyleVar();

            // Keep up at the bottom of the scroll region if we were already at the bottom at the beginning of the frame.
            // Using a scrollbar or mouse-wheel will take away from the bottom edge.
            if (AutoScroll && ImGui::GetScrollY() >= ImGui::GetScrollMaxY())
                ImGui::SetScrollHereY(1.0f);
        }
        ImGui::EndChild();
        ImGui::End();
    }
};

void UserInterface::render_log_panel() {
    static ExampleAppLog log;
    log.Draw("Example: Log");
}

void UserInterface::render_control_panel() {
    ImGui::Begin("Control Panel");
    static ImGuiTreeNodeFlags base_flags = ImGuiTreeNodeFlags_DrawLinesFull | ImGuiTreeNodeFlags_DefaultOpen;
    ImGui::CheckboxFlags("ImGuiTreeNodeFlags_DrawLinesNone", &base_flags, ImGuiTreeNodeFlags_DrawLinesNone);
    ImGui::CheckboxFlags("ImGuiTreeNodeFlags_DrawLinesFull", &base_flags, ImGuiTreeNodeFlags_DrawLinesFull);
    ImGui::CheckboxFlags("ImGuiTreeNodeFlags_DrawLinesToNodes", &base_flags, ImGuiTreeNodeFlags_DrawLinesToNodes);
    if (ImGui::TreeNodeEx("Hawkes Model", base_flags)) {
        if (ImGui::TreeNodeEx("Performance Controls", base_flags)) {
            static int n_workers = DefaultParameters::N_MAX_WORKERS;
            static int update_speed = DefaultParameters::UPDATE_SPEED;
            ImGui::SliderInt("Number of Workers", &n_workers, 1, 10);
            ImGui::SliderInt("Update Speed (per seconds)", &update_speed, 1, 200);
            ImGui::TreePop();
        }
        if (ImGui::TreeNodeEx("Calibration Controls", base_flags)) {
            ImGui::Text("Select Exchanges to Connect:");
            ImGui::Spacing();

            // On utilise un vecteur de booléens statique pour garder l'état entre les frames
            static std::vector<std::string> exchanges = {"Binance", "Coinbase", "Kraken", "Bybit", "OKX"};
            static bool states[5] = {true, true, true, false, false}; // États par défaut

            const ImVec2 button_size = ImVec2(80, 40);

            for (int i = 0; i < exchanges.size(); i++) {
                if (i > 0) ImGui::SameLine();

                ImGui::PushID(i);

                // 1. Définition de la couleur selon l'état (Vert si ON, Rouge/Gris si OFF)
                if (states[i]) {
                    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.0f, 0.6f, 0.0f, 1.0f));         // Vert
                    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.0f, 0.8f, 0.0f, 1.0f));  // Vert clair
                    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.0f, 0.4f, 0.0f, 1.0f));   // Vert foncé
                } else {
                    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.4f, 0.4f, 0.4f, 1.0f));         // Gris
                    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.6f, 0.2f, 0.2f, 1.0f));  // Rouge pâle
                    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.8f, 0.1f, 0.1f, 1.0f));   // Rouge
                }

                // 2. Dessin du bouton
                if (ImGui::Button(exchanges[i].c_str(), button_size)) {
                    states[i] = !states[i]; // Toggle de l'état
                    
                    // --- C'est ici que tu mettrais à jour ton SchedulerConfig ---
                    // config.websocket_map[exchanges[i]].active = states[i];
                }

                // 3. ON DÉPILE TOUJOURS LES COULEURS APRÈS LE BOUTON
                ImGui::PopStyleColor(3);
                ImGui::PopID();
            }
            static int calibration_duration = DefaultParameters::CALIBRATION_TIME;
            ImGui::InputInt("Calibration Duration (s)", &calibration_duration);
            ImGui::TreePop();
        }
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
        // Logique de lancement
    }

    ImGui::End();
}

void UserInterface::update_hawkes_models() {
    
}

void UserInterface::render_title_bar() {
    ImGuiViewport* viewport = ImGui::GetMainViewport();
    
    // 1. On récupère la hauteur de la barre au-dessus pour savoir de combien on doit descendre
    float selector_bar_height = ImGui::GetFrameHeight() + 8.0f; 
    
    // 2. On décale la position Y vers le bas en ajoutant 'selector_bar_height'
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

    // --- Contenu de gauche ---
    ImGui::Text("Hawkes Models Dashboard");
    ImGui::SameLine();
    ImGui::TextDisabled(" - Real-time Monitoring and Control");

    // --- Contenu de droite (Horloge) ---
    auto t = std::time(nullptr);
    auto tm = *std::localtime(&t);
    std::ostringstream oss;
    oss << std::put_time(&tm, "%Y-%m-%d %H:%M:%S");
    std::string time_str = oss.str();

    float time_text_width = ImGui::CalcTextSize(time_str.c_str()).x;
    float padding_right = ImGui::GetStyle().WindowPadding.x;
    
    ImGui::SameLine(ImGui::GetWindowWidth() - time_text_width - padding_right);
    ImGui::Text("%s", time_str.c_str());

    ImGui::End();
}

void UserInterface::render_symbol_selector() {
    if (ImGui::TreeNode("Symbols"))
    {
        for (int i = 0; i < config.symbols.size(); i++) {
            if (ImGui::Selectable(config.symbols[i].c_str(), is_symbol_selected[i])) {
                if (is_symbol_selected[i]) {
                    is_symbol_selected[i] = false;
                } else {
                    is_symbol_selected[i] = true;
                }

            }
        }
        ImGui::TreePop();
    }
}

void UserInterface::render_scrolling_buffer() {
    ImGui::Begin("Hawkes Intensities Real-Time");

    // 1. Récupération des données fraîches
    std::vector<TelemetryManager::Snapshot> snaps = telemetry_manager.get_all_snapshots();

    // On utilise un temps relatif continu et stable pour le graphique (secondes depuis démarrage)
    float t = (float)ImGui::GetTime();

    // On récupère les dernières intensités de tous les actifs sélectionnés par l'utilisateur
    // Ce sont tous les true dans is_symbol_selected
    for (int i = 0; i < config.symbols.size(); i++) {
        if (!is_symbol_selected[i])
            continue;

        if (i >= (int)snaps.size())
            continue;

        const auto& snap = snaps[i];
        if (snap.intensities.empty())
            continue;

        auto it = all_buffers.find(snap.symbol);
        if (it == all_buffers.end())
            continue;

        // Ajoute un point par intensité websocket sous la même référence de temps
        for (int k = 0; k < 1; k++) {
            it->second.AddPoint(t, (float)snap.intensities[k]);
        }
    }

    // 3. Paramètres d'affichage
    static float history = 10.0f;
    ImGui::SetNextItemWidth(150);
    ImGui::SliderFloat("History Window", &history, 1, 60, "%.1f s");

    // Paramètres d'échelle Y (L'intensité peut monter haut, on peut l'ajuster ou la mettre en auto)
    static float y_limit = 2.0f; 
    ImGui::SameLine();
    ImGui::SetNextItemWidth(150);
    ImGui::DragFloat("Max Intensity Y", &y_limit, 0.1f, 0.1f, 100.0f);

    static ImPlotAxisFlags flags = ImPlotAxisFlags_None;

    // 4. Graphique Scrolling (Défilant)
    if (ImPlot::BeginPlot("##ScrollingHawkes", ImVec2(-1, 400))) {
        ImPlot::SetupAxes("Time (s)", "λ (Intensity)", flags, flags);
        ImPlot::SetupAxisLimits(ImAxis_X1, t - history, t, ImGuiCond_Always);
        ImPlot::SetupAxisLimits(ImAxis_Y1, 0, y_limit);

        for (int i = 0; i < config.symbols.size(); i++) {
            if (is_symbol_selected[i]) {
                if (!all_buffers[snaps[i].symbol].Data.empty()) {
                    std::string label1 = "Intensity " + snaps[i].symbol;
                    ImPlot::SetNextFillStyle(IMPLOT_AUTO_COL, 0.25f);
                                    
                    ImPlot::PlotLine(label1.c_str(), 
                                    &all_buffers[snaps[i].symbol].Data[0].x, 
                                    &all_buffers[snaps[i].symbol].Data[0].y, 
                                    (int)all_buffers[snaps[i].symbol].Data.size(), 
                                    0, all_buffers[snaps[i].symbol].Offset, 2 * sizeof(float));
                }
            }
        }
        ImPlot::EndPlot();
    }
    ImGui::End();
}

int UserInterface::main_renderer() {
    initialize();

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImPlot::CreateContext();
    ImGuiIO& io = ImGui::GetIO(); (void)io;
    
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;

    #if defined(__APPLE__)
    const char* glsl_version = "#version 150";
    #else
    const char* glsl_version = "#version 130";
    #endif

    // Liaison avec GLFW
    ImGui_ImplGlfw_InitForOpenGL(this->window, true); 
    // Liaison avec OpenGL3
    ImGui_ImplOpenGL3_Init(glsl_version);

    ImVec4 clear_color = ImGui::GetStyle().Colors[ImGuiCol_WindowBg];

    bool show_demo_window = true;

    // boucle selon à temps de rafraîchissement souhaité
    // glfwSwapInterval(0); // Vsync activé pour limiter à la fréquence de rafraîchissement du moniteur (généralement 60Hz)
    double rate = 1.0 / DefaultParameters::UPDATE_SPEED; // Durée d'une frame en secondes
    double last_time = glfwGetTime();
    while(true) {
        if (glfwWindowShouldClose(this->window)) break;

        glfwPollEvents();

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        // Initialisation du dockspace pour permettre le docking des fenêtres
        ImGui::DockSpaceOverViewport(0, ImGui::GetMainViewport());

        // Met à jour l'affichage du control panel des modèles de Hawkes
        // render_title_bar();
        // render_selector_bar();
        if (show_demo_window) ImGui::ShowDemoWindow(&show_demo_window);
        update_hawkes_models();
        render_log_panel();
        render_control_panel();
        render_scrolling_buffer();
        
        // Rendu
        ImGui::Render();
        int display_w, display_h;
        glfwGetFramebufferSize(window, &display_w, &display_h);
        glViewport(0, 0, display_w, display_h);
        glClearColor(
            clear_color.x, 
            clear_color.y, 
            clear_color.z, 
            clear_color.w);
        glClear(GL_COLOR_BUFFER_BIT);

        
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

        if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable)
        {
            GLFWwindow* backup_current_context = glfwGetCurrentContext();
            ImGui::UpdatePlatformWindows();
            ImGui::RenderPlatformWindowsDefault();
            glfwMakeContextCurrent(backup_current_context);
        }

        glfwSwapBuffers(window);

        // --- GESTION DU FRAMERATE ---
        
        // 1. Déterminer à quel moment cette frame DOIT se terminer
        double target_time = last_time + rate;
        double current_time = glfwGetTime();

        // 2. Si on est en avance, on attend
        if (current_time < target_time) {
            // Calculer combien de secondes il reste à attendre
            double sleep_time = target_time - current_time;
            
            // On endort le thread pour libérer le CPU (conversion en millisecondes)
            // On retire 1 ms par sécurité car la fonction sleep de l'OS n'est pas parfaite à la microseconde près
            if (sleep_time > 0.002) { 
                std::this_thread::sleep_for(std::chrono::milliseconds(static_cast<int>((sleep_time - 0.001) * 1000)));
            }

            // Attente active très courte (busy-wait) juste pour la dernière milliseconde afin d'être super précis
            while (glfwGetTime() < target_time) {
                // spin
            }
        }

        // 3. Mettre à jour last_time de manière stable (évite le micro-stuttering)
        last_time += rate; 
        // Note: Si l'application a complètement freezé, on reset pour éviter de rattraper le retard en accéléré
        if (glfwGetTime() - last_time > 1.0) {
            last_time = glfwGetTime();
        }

        // 4. Affichage des FPS
        // Dear ImGui calcule déjà les FPS de manière lissée et ultra précise !
        // std::cout << "FPS: " << ImGui::GetIO().Framerate << std::endl;
    }

    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImPlot::DestroyContext();
    ImGui::DestroyContext();

    glfwDestroyWindow(this->window);
    glfwTerminate();

    return 0;
}