#ifndef CONFIG_H
#define CONFIG_H

#include <string>  // <== Make sure this is present

struct Config {
    enum class SchedulerType { FCFS, RR };

    int numCpu = 1;
    SchedulerType schedulerType = SchedulerType::FCFS;
    int quantum = 1;
    int batchFreq = 1;
    int minIns = 1;
    int maxIns = 5;
    int delayPerExec = 1;

    int dummyGenerationInterval = 5; // Add this here if needed

    // Declare load as static method returning Config by value
    static Config load(const std::string& filename);
};

#endif // CONFIG_H
