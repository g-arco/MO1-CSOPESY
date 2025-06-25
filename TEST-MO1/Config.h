#ifndef CONFIG_H
#define CONFIG_H

#include <string>

struct Config {
    enum class SchedulerType { FCFS, RR };

    int numCpu = 1;
    SchedulerType schedulerType = SchedulerType::FCFS;
    int quantum = 1;
    int batchFreq = 1;

    int minIns = 1;
    int maxIns = 5;
    int delayPerExec = 1;

    int dummyGenerationInterval = 5;

    static Config load(const std::string& filename);
};

#endif 
