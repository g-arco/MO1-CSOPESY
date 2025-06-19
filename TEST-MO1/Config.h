#ifndef CONFIG_H
#define CONFIG_H

#include <string>

struct Config {
    int numCpu = 1;
    std::string scheduler = "fcfs";
    int quantum = 1;
    int batchFreq = 1;
    int minIns = 1;
    int maxIns = 5;
    int delayPerExec = 1;

    static Config load(const std::string& filename);
};

#endif
