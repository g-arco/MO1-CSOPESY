#ifndef CONFIG_H
#define CONFIG_H

#include <string>  
#include <atomic>

struct Config {
    enum class SchedulerType { FCFS, RR };

    int numCpu = 4;                 // 4 cores to handle multiple processes concurrently
    int batchFreq = 1;              // Generate a new process every CPU cycle to increase load
    int minIns = 3;                 // Minimum instructions per process is low to allow short jobs
    int maxIns = 20;                // Maximum instructions per process can be quite large for long jobs
    int delayPerExec = 200;         // 200ms delay per instruction for clearer logging and observation
    std::string schedulerType = "fcfs";
    int quantum = 0;                // Small quantum to force frequent yields and rotations



    
};

extern Config config;

extern std::atomic<int> activeCores;

template <typename T1, typename T2, typename T3>
auto clamp(const T1& v, const T2& lo, const T3& hi) -> typename std::common_type<T1, T2, T3>::type {
    using CommonType = typename std::common_type<T1, T2, T3>::type;
    return (v < static_cast<CommonType>(lo)) ? static_cast<CommonType>(lo)
        : (static_cast<CommonType>(hi) < v) ? static_cast<CommonType>(hi)
        : static_cast<CommonType>(v);

}

#endif // CONFIG_H
