#include "Config.h"
#include <type_traits>

Config config;
std::atomic<int> activeCores = 0;

void Config::loadConfig(const std::string& filename) {
    std::ifstream file(filename);
    if (!file.is_open()) {
        throw std::runtime_error("Could not open config file.");
    }

    std::string parameter;

    while (file >> parameter) {
        if (parameter == "num-cpu") {
            int value;
            file >> value;
            config.numCpu = clamp(value, 1, 128);
        }
        else if (parameter == "scheduler") {
            std::string schedulerValue;
            file >> std::ws;

            char firstChar = file.peek();
            if (firstChar == '"') {
                file.get(); // remove opening quote
                std::getline(file, schedulerValue, '"'); // read until closing quote
            }
            else {
                file >> schedulerValue;
            }

            if (schedulerValue == "fcfs" || schedulerValue == "rr") {
                config.schedulerType = schedulerValue;
            }
            else {
                throw std::runtime_error("Invalid scheduler value.");
            }
        }
        else if (parameter == "quantum-cycles") {
            int value;
            file >> value;
            config.quantum = clamp(value, 1, 429496729);
        }
        else if (parameter == "batch-process-freq") {
            int value;
            file >> value;
            config.batchFreq = clamp(value, 1, 429496729);
        }
        else if (parameter == "min-ins") {
            int value;
            file >> value;
            config.minIns = clamp(value, 1, 429496729);
        }
        else if (parameter == "max-ins") {
            int value;
            file >> value;
            config.maxIns = clamp(value, 1, 429496729);
        }
        else if (parameter == "delays-per-exec") {
            int value;
            file >> value;
            config.delayPerExec = clamp(value, 0, 429496729);
        }
        else {
            std::cerr << "Unknown parameter in config file: " << parameter << std::endl;
        }
    }

    file.close();
}