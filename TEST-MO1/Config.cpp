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
        else if (parameter == "delay-per-exec") {
            int value;
            file >> value;
            config.delayPerExec = clamp(value, 0, 429496729);
        }
        else if (parameter == "max-overall-mem") {
            int value;
            file >> value;
            // Must be power of 2, range [2^6, 2^16] = [64, 65536]
            config.maxOverallMem = clamp(value, 64, 65536);
        }
        else if (parameter == "mem-per-frame") {
            int value;
            file >> value;
            // Must be power of 2, range [2^6, 2^16] = [64, 65536]
            config.memPerFrame = clamp(value, 64, 65536);
        }
        else if (parameter == "min-mem-per-proc") {
            int value;
            file >> value;
            // Must be power of 2, range [2^6, 2^16] = [64, 65536]
            config.minMemPerProc = clamp(value, 64, 65536);
        }
        else if (parameter == "max-mem-per-proc") {
            int value;
            file >> value;
            // Must be power of 2, range [2^6, 2^16] = [64, 65536]
            config.maxMemPerProc = clamp(value, 64, 65536);
        }
        else {
            std::cerr << "Unknown parameter in config file: " << parameter << std::endl;
        }
    }

    file.close();
}