#include "Scheduler.h"
#include "ProcessManager.h"
#include <iostream>
#include <chrono>
#include <thread>
#include <algorithm>
#include <random>
#include <atomic>
#include <exception>
#include <mutex>
#include <condition_variable>
#include <fstream>
#include <ctime>

extern int globalProcessId;

extern Config config;
extern std::atomic<int> activeCores;
std::atomic<int> cpuTicks(0);


class ActiveCoreGuard {
    std::atomic<int>& counter;
public:
    explicit ActiveCoreGuard(std::atomic<int>& c) : counter(c) { counter.fetch_add(1); }
    ~ActiveCoreGuard() { counter.fetch_sub(1); }
};

Scheduler::Scheduler(const Config& cfg)
    : config(cfg),
    finished(false),
    numCores(cfg.numCpu),
    quantumCycles(cfg.quantum),
    generatingDummies(false),
    dummyCounter(0)
{
    std::string lowerType = config.schedulerType;
    std::transform(lowerType.begin(), lowerType.end(), lowerType.begin(), ::tolower);
    schedulerType = (lowerType == "rr") ? InternalSchedulerType::RR : InternalSchedulerType::FCFS;
}

Scheduler::~Scheduler() {
    std::cout << "[Scheduler] Destructor called. Shutting down...\n";
    finish();
    stopDummyGeneration();
    joinAll();
    std::cout << "[Scheduler] Destructor finished.\n";
}

void Scheduler::start() {
    std::cout << "[Scheduler] Starting worker threads on " << numCores << " cores.\n";
    try {
        for (int i = 0; i < numCores; ++i) {
            cores.emplace_back(&Scheduler::worker, this, i);
        }
    }
    catch (const std::exception& e) {
        std::cerr << "[Scheduler] Failed to start worker threads: " << e.what() << '\n';
    }
    catch (...) {
        std::cerr << "[Scheduler] Unknown error starting worker threads.\n";
    }
}

void Scheduler::joinAll() {
    for (auto& thread : cores) {
        if (thread.joinable()) {
            std::cout << "[Scheduler] Joining worker thread.\n";
            thread.join();
        }
    }
    if (dummyThread.joinable()) {
        std::cout << "[Scheduler] Joining dummy generation thread.\n";
        dummyThread.join();
    }
}

void Scheduler::addProcess(const std::shared_ptr<Screen>& process) {
    {
        std::lock_guard<std::mutex> lock(queueMutex);
        screenQueue.push(process);
        
    }
    cv.notify_one();
}

void Scheduler::finish() {
    std::cout << "[Scheduler] Signaling finish to all threads.\n";
    finished.store(true);
    cv.notify_all();
}

void Scheduler::worker(int coreId) {
    std::cout << "[Scheduler] Worker thread started on core " << coreId << ".\n";

    while (true) {
        std::shared_ptr<Screen> screen;
        {
            std::unique_lock<std::mutex> lock(queueMutex);
            cv.wait(lock, [this] { return finished.load() || !screenQueue.empty(); });

            if (finished.load() && screenQueue.empty()) {
                std::cout << "[Scheduler] Worker thread on core " << coreId << " exiting.\n";
                return;
            }

            if (!screenQueue.empty()) {
                screen = screenQueue.front();
                screenQueue.pop();
                screen->setCoreAssigned(coreId);
                screen->setScheduled(true);

            }
        }

        if (screen) {
            ActiveCoreGuard guard(activeCores);
            screen->setStatus(ProcessStatus::RUNNING);

            if (schedulerType == InternalSchedulerType::FCFS) {
                executeProcessFCFS(screen, coreId);
            }
            else {
                executeProcessRR(screen, coreId);
            }
        }
    }
}

void Scheduler::executeProcessFCFS(const std::shared_ptr<Screen>& screen, int coreId) {
    try {
        screen->setCoreAssigned(coreId);
        std::ofstream logFile(screen->getName() + ".txt");

        while (!screen->isFinished() && !finished.load()) {
            if (screen->getCurrentInstruction() >= screen->getTotalInstructions()) {
                screen->setStatus(ProcessStatus::FINISHED);
                screen->printLog("Process finished execution.");
                break;
            }

            std::cout << "[Scheduler][FCFS] Core " << coreId << " executing instruction "
                << screen->getCurrentInstruction() + 1 << " / "
                << screen->getTotalInstructions() << " on process '"
                << screen->getName() << "'\n";

            for (int i = 0; i < config.delayPerExec; ++i) {
                ++cpuTicks;
            }

            try {
                auto timestamp = currentTimestamp();
                logFile << timestamp << " Core:" << coreId
                    << " \"Hello world from " << screen->getName() << "!\"\n";
                logFile.flush();

                screen->advanceInstruction();
            }
            catch (const std::exception& e) {
                handleProcessError(screen, e.what());
                break;
            }
        }

        if (!screen->hasError()) {
            screen->setStatus(ProcessStatus::FINISHED);
            screen->printLog("FCFS: Process completed on core " + std::to_string(coreId));
            std::cout << "[Scheduler][FCFS] Process '" << screen->getName()
                << "' finished on core " << coreId << ".\n";
        }
    }
    catch (const std::exception& e) {
        std::cerr << "[Scheduler][FCFS][Exception] Process '" << screen->getName()
            << "' on core " << coreId << " threw exception: " << e.what() << "\n";
    }
}

void Scheduler::executeProcessRR(const std::shared_ptr<Screen>& screen, int coreId) {
    try {
        screen->setCoreAssigned(coreId);
        std::ofstream logFile(screen->getName() + ".txt", std::ios::app);
        int executed = 0;

        while (!screen->isFinished() && executed < quantumCycles && !finished.load()) {
            if (screen->getCurrentInstruction() >= screen->getTotalInstructions()) {
                screen->setStatus(ProcessStatus::FINISHED);
                break;
            }

            for (int i = 0; i < config.delayPerExec; ++i) {
                ++cpuTicks;
            }

            try {
                auto timestamp = currentTimestamp();
                logFile << timestamp << " Core:" << coreId
                    << " \"Executing instruction " << screen->getCurrentInstruction()
                    << " from process " << screen->getName() << "\"\n";
                logFile.flush();

                screen->advanceInstruction();

                if (screen->hasError()) {
                    handleProcessError(screen, "Error encountered during instruction execution.");
                    break;
                }

                ++executed;
            }
            catch (const std::exception& e) {
                handleProcessError(screen, e.what());
                break;
            }
        }

        if (!screen->hasError()) {
            if (screen->getCurrentInstruction() >= screen->getTotalInstructions()) {
                screen->setStatus(ProcessStatus::FINISHED);

            }
            else {
                screen->setStatus(ProcessStatus::READY);
                addProcess(screen);

            }
        }
    }
    catch (const std::exception& e) {

    }
}

void Scheduler::startDummyGeneration() {
    bool expected = false;
    if (!generatingDummies.compare_exchange_strong(expected, true)) {
        std::cout << "[Scheduler] Dummy generation already running.\n";
        return;
    }

    if (dummyThread.joinable()) {
        dummyThread.join();
    }

    dummyThread = std::thread(&Scheduler::dummyProcessLoop, this);
}

void Scheduler::stopDummyGeneration() {
    generatingDummies.store(false);
    if (dummyThread.joinable()) {
        dummyThread.join();
    }
    std::cout << "[Scheduler] Dummy generation stopped.\n";
}

void Scheduler::dummyProcessLoop() {
    std::cout << "[Scheduler] Dummy process generation started.\n";

    try {
        std::random_device rd;
        std::mt19937 gen(rd());
        std::uniform_int_distribution<> dist(config.minIns, config.maxIns);

        dummyCounter = 0;
        auto lastGenTime = std::chrono::steady_clock::now();

        while (generatingDummies.load()) {
            auto now = std::chrono::steady_clock::now();
            auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(now - lastGenTime).count();

            if (dummyCounter >= 50) {
                std::cout << "[Scheduler] Dummy process limit reached (50). Stopping generation.\n";
                break;
            }

            if (elapsedMs >= config.batchFreq) {
                std::string name = "process" + std::to_string(++dummyCounter);
                std::cout << "[Scheduler] Generating dummy process: " << name << " (ID: " << globalProcessId << ")\n";

                auto screen = std::make_shared<Screen>();
                screen->setName(name);
                screen->generateDummyInstructions();

                int instructionCount = dist(gen);
                screen->truncateInstructions(instructionCount);
                screen->setProcessId(globalProcessId++);
                screen->setStatus(ProcessStatus::READY);
				ProcessManager::registerProcess(screen);
                addProcess(screen);

                lastGenTime = now;
            }
            else {
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
        }
    }
    catch (const std::exception& e) {
        std::cerr << "[Scheduler] Exception in dummyProcessLoop: " << e.what() << "\n";
    }
    catch (...) {
        std::cerr << "[Scheduler] Unknown exception in dummyProcessLoop.\n";
    }

    std::cout << "[Scheduler] Dummy process generation ended.\n";
}

std::string Scheduler::currentTimestamp() {
    time_t now = time(nullptr);
    tm ltm{};
#ifdef _WIN32
    localtime_s(&ltm, &now);
#else
    localtime_r(&now, &ltm);
#endif
    char buffer[25];
    strftime(buffer, sizeof(buffer), "(%m/%d/%Y %I:%M:%S %p)", &ltm);
    return std::string(buffer);
}

void Scheduler::handleProcessError(const std::shared_ptr<Screen>& screen, const std::string& message) {
    screen->setError(true);
    screen->setStatus(ProcessStatus::FINISHED);
    screen->printLog("Error during instruction execution: " + message);
    std::cerr << "[Scheduler][ProcessError] Process '" << screen->getName()
        << "' encountered an error: " << message << "\n";
}

