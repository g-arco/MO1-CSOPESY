#include "Scheduler.h"
#include "ProcessManager.h"
#include <iostream>
#include <chrono>

Scheduler::Scheduler(const Config& cfg) : config(cfg), running(false) {}

Scheduler::~Scheduler() {
    stopSchedulerThread();
    for (auto& w : workers) {
        if (w.joinable()) w.join();
    }
}

void Scheduler::addProcess(Screen* process) {
    std::unique_lock<std::mutex> lock(queueMutex);
    processQueue.push(process);
    cv.notify_one();
}

void Scheduler::startSchedulerThread() {
    running = true;
    schedulerThread = std::thread(&Scheduler::schedulerLoop, this);

    for (int i = 0; i < config.numCpu; ++i) {
        workers.emplace_back(&Scheduler::worker, this, i);
    }
}

void Scheduler::stopSchedulerThread() {
    running = false;
    cv.notify_all();
    if (schedulerThread.joinable()) schedulerThread.join();
}

void Scheduler::schedulerLoop() {
    int processCount = 0;
    while (running) {
        std::this_thread::sleep_for(std::chrono::milliseconds(config.batchFreq * 100));
        std::vector<Instruction> instructions = {
            { InstructionType::PRINT, { "Hello world!" } }
        };
        std::string name = "p" + std::to_string(++processCount);
        auto screen = new Screen(name, instructions);
        ProcessManager::registerProcess(std::shared_ptr<Screen>(screen));
        addProcess(screen);
    }
}

void Scheduler::worker(int coreId) {
    while (running) {
        Screen* task = nullptr;
        {
            std::unique_lock<std::mutex> lock(queueMutex);
            cv.wait(lock, [&] { return !processQueue.empty() || !running; });
            if (!processQueue.empty()) {
                task = processQueue.front();
                processQueue.pop();
            }
        }
        if (task && !task->isFinished()) {
            task->setCoreAssigned(coreId);
            task->executeNextInstruction();
        }
    }
}
