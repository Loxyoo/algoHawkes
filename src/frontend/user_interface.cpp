#include "user_interface.h"



static void glfw_error_callback(int error, const char* description) {
    fprintf(stderr, "Glfw Error %d: %s\n", error, description);
}

UserInterface::UserInterface(SchedulerConfig& config, TelemetryManager& telemetry_manager) : config(config), telemetry_manager(telemetry_manager) {

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

void UserInterface::render_scrolling_buffer() {
    ImGui::Begin("Hawkes models");
    static ScrollingBuffer sdata1, sdata2;
    static RollingBuffer   rdata1, rdata2;
    ImVec2 mouse = ImGui::GetMousePos();

    // Add points to the buffers every 0.02 seconds
    static float t = 0, last_t = 0.0f;
    if (t == 0 || t - last_t >= 0.02f) {
        sdata1.AddPoint(t, mouse.x * 0.0005f);
        rdata1.AddPoint(t, mouse.x * 0.0005f);
        sdata2.AddPoint(t, mouse.y * 0.0005f);
        rdata2.AddPoint(t, mouse.y * 0.0005f);
        last_t = t;
    }
    t += ImGui::GetIO().DeltaTime;

    static float history = 10.0f;
    ImGui::SliderFloat("History",&history,1,30,"%.1f s");
    rdata1.Span = history;
    rdata2.Span = history;

    static ImPlotAxisFlags flags = ImPlotAxisFlags_NoTickLabels;

    if (ImPlot::BeginPlot("##Scrolling", ImVec2(-1,ImGui::GetTextLineHeight()*10))) {
        ImPlot::SetupAxes(nullptr, nullptr, flags, flags);
        ImPlot::SetupAxisLimits(ImAxis_X1,t - history, t, ImGuiCond_Always);
        ImPlot::SetupAxisLimits(ImAxis_Y1,0,1);
        ImPlot::SetNextFillStyle(IMPLOT_AUTO_COL,0.5f);
        ImPlot::PlotShaded("Mouse X", &sdata1.Data[0].x, &sdata1.Data[0].y, sdata1.Data.size(), -INFINITY, 0, sdata1.Offset, 2 * sizeof(float));
        ImPlot::PlotLine("Mouse Y", &sdata2.Data[0].x, &sdata2.Data[0].y, sdata2.Data.size(), 0, sdata2.Offset, 2*sizeof(float));
        ImPlot::EndPlot();
    }
    if (ImPlot::BeginPlot("##Rolling", ImVec2(-1,ImGui::GetTextLineHeight()*10))) {
        ImPlot::SetupAxes(nullptr, nullptr, flags, flags);
        ImPlot::SetupAxisLimits(ImAxis_X1,0,history, ImGuiCond_Always);
        ImPlot::SetupAxisLimits(ImAxis_Y1,0,1);
        ImPlot::PlotLine("Mouse X", &rdata1.Data[0].x, &rdata1.Data[0].y, rdata1.Data.size(), 0, 0, 2 * sizeof(float));
        ImPlot::PlotLine("Mouse Y", &rdata2.Data[0].x, &rdata2.Data[0].y, rdata2.Data.size(), 0, 0, 2 * sizeof(float));
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
    double rate = 1.0 / DefaultParameters::UPDATE_SPEED;
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

        if (last_time + rate < glfwGetTime()) {
            // Attendre la différence de temps pour atteindre le taux de rafraîchissement souhaité
            while (last_time + rate >= glfwGetTime()) {
                // Attente active (busy-wait) pour une précision maximale
            }
            last_time = glfwGetTime();
        }
        // Afficher le nombre de fps dans le terminal pour debug
        std::cout << "FPS: " << 1.0 / (glfwGetTime() - last_time) << std::endl;
    }

    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImPlot::DestroyContext();
    ImGui::DestroyContext();

    glfwDestroyWindow(this->window);
    glfwTerminate();

    return 0;
}