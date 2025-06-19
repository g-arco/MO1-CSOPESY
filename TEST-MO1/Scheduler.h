#ifndef SCHEDULER_H
#define SCHEDULER_H

#include "Instruction.h"
#include "Config.h"
#include "Screen.h"
#include <thread>
#include <vector>
#include <queue>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <memory>
#include <sstream>
#include <iomanip>

class Scheduler {
public:
    explicit Scheduler(const Config& config);
    ~Scheduler();

    void start();
    void stop();

    void addProcess(std::shared_ptr<Screen> process);

    // New methods for dummy process generation
    void startDummyGeneration();
    void stopDummyGeneration();

private:
    void fcfsLoop();
    void rrLoop();

    void dummyProcessGenerator(); // thread function to generate dummy processes

    Config config;
    int quantumCycles;
    int delayPerExec;
    int dummyGenerationInterval;

    std::queue<std::shared_ptr<Screen>> readyQueue;
    std::vector<bool> cpuCores; // true = busy, false = free

    std::mutex queueMutex;
    std::condition_variable cv;
    std::atomic<bool> running;

    enum class SchedulerType { FCFS, RR } schedulerType;

    std::thread schedulerThread;

    // Dummy process generation members
    std::atomic<bool> generateDummyProcesses;
    std::thread generatorThread;
    std::atomic<int> dummyProcessCounter;
};

#endif // SCHEDULER_H
