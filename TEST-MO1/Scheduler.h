#ifndef SCHEDULER_H
#define SCHEDULER_H

#include "Screen.h"
#include "Config.h"
#include <queue>
#include <vector>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <memory>

// Forward declaration to avoid circular dependency
class Screen;

// Internal scheduler type enumeration
enum class InternalSchedulerType {
    FCFS,  // First Come First Serve
    RR     // Round Robin
};

class Scheduler {
private:
    // Configuration and state
    Config config;
    std::atomic<bool> finished;
    int numCores;
    int quantumCycles;
    InternalSchedulerType schedulerType;

    std::thread cleanupThread;
    std::atomic<bool> cleanupRunning{ false };

    // Process queue and synchronization
    std::queue<std::shared_ptr<Screen>> screenQueue;
    mutable std::mutex queueMutex;
    std::condition_variable cv;

    // Worker threads
    std::vector<std::thread> cores;

    // Dummy process generation
    std::atomic<bool> generatingDummies;
    std::thread dummyThread;
    int dummyCounter;

    // Private methods
    void worker(int coreId);
    void executeProcessFCFS(const std::shared_ptr<Screen>& screen, int coreId);
    void executeProcessRR(const std::shared_ptr<Screen>& screen, int coreId);
    void dummyProcessLoop();
    std::string currentTimestamp();
    void handleProcessError(const std::shared_ptr<Screen>& screen, const std::string& message);

public:
    // Constructor and destructor
    explicit Scheduler(const Config& cfg);
    ~Scheduler();


    void startCleanupThread();
    void cleanupLoop();
    void stopCleanupThread();
    // Core functionality
    void start();
    void finish();
    void joinAll();

    // Process management
    void addProcess(const std::shared_ptr<Screen>& process);

    // Dummy process generation
    void startDummyGeneration();
    void stopDummyGeneration();

    // Getters
    int getNumCores() const { return numCores; }
    int getQuantumCycles() const { return quantumCycles; }
    InternalSchedulerType getSchedulerType() const { return schedulerType; }
    bool isFinished() const { return finished.load(); }
    bool isGeneratingDummies() const { return generatingDummies.load(); }
    int getDummyCounter() const { return dummyCounter; }

    // Statistics
    size_t getQueueSize() const {
        std::lock_guard<std::mutex> lock(queueMutex);
        return screenQueue.size();
    }
};


#endif // SCHEDULER_H