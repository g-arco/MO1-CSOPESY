#include "Config.h"
#include <fstream>
#include <sstream>
#include <iostream>

Config Config::load(const std::string& filename) {
    Config config;

    std::ifstream file(filename);
    if (!file.is_open()) {
        std::cerr << "Failed to open config file: " << filename << std::endl;
        return config; // returns default config
    }

    std::string line;
    while (std::getline(file, line)) {
        std::istringstream iss(line);
        std::string key;
        if (!(iss >> key)) continue;

        if (key == "num-cpu") {
            int val;
            if (iss >> val) config.numCpu = val;
        }
        else if (key == "scheduler") {
            std::string val;
            if (iss >> val) {
                if (val == "fcfs")
                    config.schedulerType = Config::SchedulerType::FCFS;
                else if (val == "rr")
                    config.schedulerType = Config::SchedulerType::RR;
            }
        }
        else if (key == "quantum-cycles") {
            int val;
            if (iss >> val) config.quantum = val;
        }
        else if (key == "batch-process-freq") {
            int val;
            if (iss >> val) config.batchFreq = val;
        }
        else if (key == "min-ins") {
            int val;
            if (iss >> val) config.minIns = val;
        }
        else if (key == "max-ins") {
            int val;
            if (iss >> val) config.maxIns = val;
        }
        else if (key == "delays-per-exec") {
            int val;
            if (iss >> val) config.delayPerExec = val;
        }
    }

    return config;
}
