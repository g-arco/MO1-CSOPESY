#ifndef SCHEDULER_H
#define SCHEDULER_H

#include "Instruction.h"
#include "Config.h"
#include "Screen.h"
#include <vector>
#include <queue>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <atomic>

class Scheduler {
public:
    Scheduler(const Config& config);
    ~Scheduler();

    void addProcess(Screen* process);
    void startSchedulerThread();
    void stopSchedulerThread();

private:
    void schedulerLoop();
    void worker(int coreId);

    Config config;
    int tick = 0;
    std::vector<std::thread> workers;
    std::queue<Screen*> processQueue;
    std::mutex queueMutex;
    std::condition_variable cv;
    std::atomic<bool> running;
    std::thread schedulerThread;
};

#endif
