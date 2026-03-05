#ifndef CALIBRATION_H
#define CALIBRATION_H
#include <json/value.h>
#include <json/json.h>
#include <thread>
#include <fstream>
#include <filesystem>
#include <iostream>
#include <cassert>


#include "../struct.h"
#include "../genericWS.h"
#include "../tools.h"

/**
 * @class Calibration Engine
 * @brief Manages the volume mesurements and the charge repartitioning (load balancing).
 */
class CalibrationEngine {
    private:

    protected:
        ThreadSafeQueue<normalized_data>& inqueue;
        Json::Value assets_map;
    
    public:
        CalibrationEngine(
            ThreadSafeQueue<normalized_data>& q,
            Json::Value assets_map
        ) : inqueue(q) {
            this->assets_map = assets_map;
        }

        std::vector<volume_symbol> evaluate_assets_volumes(int duration);

        void run();

};



#endif // CALIBRATION_H