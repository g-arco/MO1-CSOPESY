#include "Config.h"
#include <fstream>
#include <sstream>
#include <iostream>

Config Config::load(const std::string& filename) {
    Config cfg;
    std::ifstream file(filename);
    std::string key;
    while (file >> key) {
        if (key == "num-cpu") file >> cfg.numCpu;
        else if (key == "scheduler") file >> cfg.scheduler;
        else if (key == "quantum-cycles") file >> cfg.quantum;
        else if (key == "batch-process-freq") file >> cfg.batchFreq;
        else if (key == "min-ins") file >> cfg.minIns;
        else if (key == "max-ins") file >> cfg.maxIns;
        else if (key == "delays-per-exec") file >> cfg.delayPerExec;
    }
    return cfg;
}
