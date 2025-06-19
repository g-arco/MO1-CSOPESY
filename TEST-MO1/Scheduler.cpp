#include "Scheduler.h"
#include <iostream>
#include <chrono>
#include <iomanip>
#include <sstream>

// Constructor
Scheduler::Scheduler(const Config& cfg)
    : config(cfg),
    quantumCycles(cfg.quantum),
    delayPerExec(cfg.delayPerExec),
    running(false),
    generateDummyProcesses(false),
    dummyGenerationInterval(cfg.dummyGenerationInterval),
    dummyProcessCounter(0)
{
    cpuCores.resize(config.numCpu, false);

    if (config.schedulerType == Config::SchedulerType::FCFS)
        schedulerType = SchedulerType::FCFS;
    else if (config.schedulerType == Config::SchedulerType::RR)
        schedulerType = SchedulerType::RR;
    else {
        std::cerr << "Unknown scheduler type, defaulting to FCFS\n";
        schedulerType = SchedulerType::FCFS;
    }
}

Scheduler::~Scheduler() {
    stop();
    stopDummyGeneration();

    if (schedulerThread.joinable())
        schedulerThread.join();

    if (generatorThread.joinable())
        generatorThread.join();
}

// Start scheduling threads
void Scheduler::start() {
    running = true;

    if (schedulerType == SchedulerType::FCFS) {
        schedulerThread = std::thread(&Scheduler::fcfsLoop, this);
    }
    else if (schedulerType == SchedulerType::RR) {
        schedulerThread = std::thread(&Scheduler::rrLoop, this);
    }
}

// Stop scheduling threads
void Scheduler::stop() {
    running = false;
    cv.notify_all();
}

// Add process to ready queue
void Scheduler::addProcess(std::shared_ptr<Screen> process) {
    {
        std::lock_guard<std::mutex> lock(queueMutex);
        readyQueue.push(process);
        //std::cout << "[Scheduler] Added process: " << process->getName() << std::endl;
    }
    cv.notify_one();
}

// Start dummy process generation
void Scheduler::startDummyGeneration() {
    if (generateDummyProcesses) return; // already running
    generateDummyProcesses = true;
    generatorThread = std::thread(&Scheduler::dummyProcessGenerator, this);
    //std::cout << "[Scheduler] Dummy process generation started.\n";
}

// Stop dummy process generation
void Scheduler::stopDummyGeneration() {
    if (!generateDummyProcesses) return;
    generateDummyProcesses = false;
    if (generatorThread.joinable())
        generatorThread.join();
    //std::cout << "[Scheduler] Dummy process generation stopped.\n";
}

// Dummy process generator loop
void Scheduler::dummyProcessGenerator() {
    while (generateDummyProcesses) {
        // Wait for the configured interval (based on CPU tick delay)
        std::this_thread::sleep_for(std::chrono::milliseconds(delayPerExec * dummyGenerationInterval));

        // Generate new dummy process
        auto newProcess = std::make_shared<Screen>();
        std::stringstream ss;
        ss << "p" << std::setw(2) << std::setfill('0') << ++dummyProcessCounter;
        newProcess->setName(ss.str());
        newProcess->generateDummyInstructions();

        addProcess(newProcess);
        //std::cout << "[Generator] Created dummy process: " << ss.str() << std::endl;
    }
}

// FCFS scheduling loop
void Scheduler::fcfsLoop() {
    while (running) {
        std::unique_lock<std::mutex> lock(queueMutex);
        //std::cout << "[Scheduler] FCFS waiting for processes..." << std::endl;
        cv.wait(lock, [this]() { return !readyQueue.empty() || !running; });
        if (!running) break;

        //std::cout << "[Scheduler] FCFS processing processes..." << std::endl;

        for (size_t i = 0; i < cpuCores.size(); ++i) {
            if (!cpuCores[i] && !readyQueue.empty()) {
                auto process = readyQueue.front();
                readyQueue.pop();
                cpuCores[i] = true;

                process->setCoreAssigned(static_cast<int>(i));
                //std::cout << "[Scheduler] Launching process " << process->getName() << " on core " << i << std::endl;

                std::thread([this, process, i]() {
                    process->setStatus(ProcessStatus::RUNNING);
                    while (!process->isFinished() && running) {
                        process->executeNextInstruction();
                        std::this_thread::sleep_for(std::chrono::milliseconds(100));
                    }
                    {
                        std::lock_guard<std::mutex> guard(queueMutex);
                        cpuCores[i] = false;
                        process->setCoreAssigned(-1);
                    }
                    //std::cout << "[Scheduler] Process " << process->getName() << " finished on core " << i << std::endl;
                    cv.notify_one();
                    }).detach();
            }
        }
    }
}

// Round Robin scheduling loop
void Scheduler::rrLoop() {
    while (running) {
        std::unique_lock<std::mutex> lock(queueMutex);
        cv.wait(lock, [this]() { return !readyQueue.empty() || !running; });
        if (!running) break;

        for (size_t i = 0; i < cpuCores.size(); ++i) {
            if (!cpuCores[i] && !readyQueue.empty()) {
                auto process = readyQueue.front();
                readyQueue.pop();
                cpuCores[i] = true;

                process->setCoreAssigned(static_cast<int>(i));
                std::cout << "[Scheduler] Launching process " << process->getName() << " on core " << i << " (RR)" << std::endl;

                std::thread([this, process, i]() {
                    process->setStatus(ProcessStatus::RUNNING);
                    int cycle = 0;
                    while (!process->isFinished() && cycle < quantumCycles && running) {
                        process->executeNextInstruction();
                        std::this_thread::sleep_for(std::chrono::milliseconds(100));
                        ++cycle;
                    }

                    {
                        std::lock_guard<std::mutex> guard(queueMutex);
                        cpuCores[i] = false;
                        if (!process->isFinished()) {
                            process->setStatus(ProcessStatus::READY);
                            readyQueue.push(process);
                        }
                        else {
                            process->setCoreAssigned(-1);
                        }
                    }
                    cv.notify_one();
                    }).detach();
            }
        }
    }
}
