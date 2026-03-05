#include <cstdlib>
#include "calibration.h"
#include <chrono>

std::vector<volume_symbol> CalibrationEngine::evaluate_assets_volumes(int duration) {
    std::cout << "Evaluation des volumes Assets." << std::endl;
    int n_assets = this->assets_map.size();
    std::vector<volume_symbol> assets_volume(this->assets_map.size());
    for (int i = 0; i < n_assets; i++) {
        assets_volume[i].symbol = "";
        assets_volume[i].volume = 0;
    }
    using namespace std::chrono_literals;
    auto work_duration = std::chrono::seconds(duration);
    auto start = std::chrono::steady_clock::now();

    while ((std::chrono::steady_clock::now() - start) < work_duration) {
        // std::cout << "Données recu!" << std::endl;
        normalized_data data = this->inqueue.pop();

        int index = this->assets_map[data.symbol].asInt();
        if (assets_volume[index].symbol == "") {
            assets_volume[index].symbol = data.symbol;
        }
        assets_volume[index].volume++;
        // for (volume_symbol volume: assets_volume) std::cout << volume.volume << ' ';
    }
    std::cout << "Arret de l'évaluation des assets..." << std::endl;
    return assets_volume;
}

void CalibrationEngine::run() {

}