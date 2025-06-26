#pragma once

#include <string>
#include <vector>
#include <queue>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <memory>
#include <atomic>

#include "Config.h"
#include "Screen.h"

// Scheduler class responsible for managing processes and CPU cores
class Scheduler {
public:
    explicit Scheduler(const Config& cfg);
    ~Scheduler();

    void start();
    void finish();

    void addProcess(const std::shared_ptr<Screen>& process);

    void startDummyGeneration();
    void stopDummyGeneration();

private:
    enum class InternalSchedulerType { FCFS, RR };

    // Worker thread function for each CPU core
    void worker(int coreId);

    // Scheduling strategies
    void executeProcessFCFS(const std::shared_ptr<Screen>& screen, int coreId);
    void executeProcessRR(const std::shared_ptr<Screen>& screen, int coreId);

    // Dummy process generation thread loop
    void dummyProcessLoop();

    // Utility helpers
    std::string currentTimestamp();
    void handleProcessError(const std::shared_ptr<Screen>& screen, const std::string& message);
    void busyWait(int delayMs);

    // Wait for all threads to join
    void joinAll();

    // Configuration and state
    const Config& config;

    std::atomic<bool> finished;
    std::atomic<bool> generatingDummies;

    InternalSchedulerType schedulerType;

    int numCores;
    int quantumCycles;

    std::vector<std::thread> cores;
    std::thread dummyThread;

    std::mutex queueMutex;
    std::condition_variable cv;
    std::queue<std::shared_ptr<Screen>> screenQueue;

    int dummyCounter;
};
