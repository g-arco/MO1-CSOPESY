#ifndef CONFIG_H
#define CONFIG_H

#include <string>
#include <atomic>
#include <iostream>   // for std::cerr, std::endl
#include <fstream>    // for std::ifstream
#include <stdexcept>  // for std::runtime_error

struct Config {
    enum class SchedulerType { FCFS, RR };

    int numCpu = 4;
    int batchFreq = 1;
    int minIns = 3;
    int maxIns = 20;
    int delayPerExec = 200;
    std::string schedulerType = "fcfs";
    int quantum = 0;

    void loadConfig(const std::string& filename);
};

extern Config config;  // renamed from config
extern std::atomic<int> activeCores;

template <typename T1, typename T2, typename T3>
auto clamp(const T1& v, const T2& lo, const T3& hi) -> typename std::common_type<T1, T2, T3>::type {
    using CommonType = typename std::common_type<T1, T2, T3>::type;
    return (v < static_cast<CommonType>(lo)) ? static_cast<CommonType>(lo)
        : (static_cast<CommonType>(hi) < v) ? static_cast<CommonType>(hi)
        : static_cast<CommonType>(v);
}

#endif // CONFIG_H
