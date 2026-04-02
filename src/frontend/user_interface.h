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


namespace DefaultParameters {
    static constexpr int WindowWidth = 1280;
    static constexpr int WindowHeight = 720;
    static const std::string title = "Bloomberg Like Terminal";
    static constexpr int CALIBRATION_TIME = 10;
    static constexpr int TRAINING_TIME = 30;
    static constexpr int UPDATE_SPEED = 60;
    static constexpr int N_MAX_WORKERS = 2;
    static const char* websockets[] = {"All", "Binance", "Coinbase", "Kraken", "Bybit", "OKX" };
    static const char* models[] = {"Hawkes", "LSTM", "Transformer"};
    static const std::string default_symbol = "BTCUSD";
    static const std::string current_symbol = "All";
    static const std::string current_exchange = "All";
}

struct TickerItem {
    std::string name;
    double price;
    double change;
};

class UserInterface {
    protected:
        SchedulerConfig& config;
        TelemetryManager& telemetry_manager;
        GLFWwindow* window;
        int current_symbol_index = config.symbols_map[DefaultParameters::default_symbol].asInt();
        std::vector<bool> is_symbol_selected;


        // utility structure for realtime plot
        struct ScrollingBuffer {
            int MaxSize;
            int Offset;
            ImVector<ImVec2> Data;
            ScrollingBuffer(int max_size = 2000) {
                MaxSize = max_size;
                Offset  = 0;
                Data.reserve(MaxSize);
            }
            void AddPoint(float x, float y) {
                if (Data.size() < MaxSize)
                    Data.push_back(ImVec2(x,y));
                else {
                    Data[Offset] = ImVec2(x,y);
                    Offset =  (Offset + 1) % MaxSize;
                }
            }
            void Erase() {
                if (Data.size() > 0) {
                    Data.shrink(0);
                    Offset  = 0;
                }
            }
        };

        std::map<std::string, ScrollingBuffer> all_buffers;

        // utility structure for realtime plot
        struct RollingBuffer {
            float Span;
            ImVector<ImVec2> Data;
            RollingBuffer() {
                Span = 10.0f;
                Data.reserve(2000);
            }
            void AddPoint(float x, float y) {
                float xmod = fmodf(x, Span);
                if (!Data.empty() && xmod < Data.back().x)
                    Data.shrink(0);
                Data.push_back(ImVec2(xmod, y));
            }
        };

    public:
        UserInterface(SchedulerConfig& config, TelemetryManager& telemetry_manager);

        /**
         * @brief Initializes the user interface, setting up necessary resources and configurations. This function will be called before entering the main rendering loop to ensure that the UI is ready to display data and respond to user interactions.
         */
        int initialize();

        /**
         * @brief Update the Hawkes models with the latest data. This function will be called periodically to refresh the models based on new market data and user interactions.
         */
        void update_hawkes_models();
        void render_scrolling_buffer();
        void render_title_bar();
        void render_selector_bar();
        void render_control_panel();
        void render_instrument_panel();
        void render_log_panel();
        void render_ticker_tape();
        void render_symbol_selector();

        /**
         * @brief This function will be responsible for updating all models. It will be called in the main loop to ensure that the models are kept up-to-date with the latest data and user interactions.
         */
        void main_updater();

        /**
         * @brief Main rendering loop for the user interface. This function will be called to start the GUI and will run until the user closes the window.
         */
        int main_renderer();
};

class HawkesInterface : public UserInterface {
    protected:

    public:
        HawkesInterface(SchedulerConfig& config, TelemetryManager& telemetry_manager) : UserInterface(config, telemetry_manager) {}
        void render_instrument_panel();
};

class MainMenuInterface : public UserInterface {
    protected:

    public:
        MainMenuInterface(SchedulerConfig& config, TelemetryManager& telemetry_manager) : UserInterface(config, telemetry_manager) {}
};

#endif // USER_INTERFACE_H
