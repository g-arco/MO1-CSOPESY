#include "Scheduler.h"
#include "ProcessManager.h"
#include "MemoryManager.h"

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
// CHANGE: MCO2 - Include external memory manager
extern std::unique_ptr<MemoryManager> memoryManager;
extern std::shared_ptr<ProcessManager> processManager;

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

    if (memoryManager) {
        memoryManager->initializeCores(numCores);
    }
}

Scheduler::~Scheduler() {
    /*std::cout << "[Scheduler] Destructor called. Shutting down...\n";*/
    finish();
    stopDummyGeneration();
    joinAll();
    /* std::cout << "[Scheduler] Destructor finished.\n";*/
}

void Scheduler::start() {
    /*std::cout << "[Scheduler] Starting worker threads on " << numCores << " cores.\n";*/
    try {
        for (int i = 0; i < numCores; ++i) {
            cores.emplace_back(&Scheduler::worker, this, i);
        }
    }
    catch (const std::exception& e) {
        /*std::cerr << "[Scheduler] Failed to start worker threads: " << e.what() << '\n';*/
    }
    catch (...) {
        /* std::cerr << "[Scheduler] Unknown error starting worker threads.\n";*/
    }
}

void Scheduler::joinAll() {
    for (auto& thread : cores) {
        if (thread.joinable()) {
            /*std::cout << "[Scheduler] Joining worker thread.\n";*/
            thread.join();
        }
    }
    if (dummyThread.joinable()) {
        /*std::cout << "[Scheduler] Joining dummy generation thread.\n";*/
        dummyThread.join();
    }

    if (cleanupThread.joinable()) {
        cleanupThread.join();
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
    /*std::cout << "[Scheduler] Signaling finish to all threads.\n";*/
    finished.store(true);
    cv.notify_all();
}

void Scheduler::worker(int coreId) {
    auto lastTickTime = std::chrono::steady_clock::now();

    while (true) {
        std::shared_ptr<Screen> screen;
        bool hasWork = false;

        {
            std::unique_lock<std::mutex> lock(queueMutex);

            // CRITICAL: Use timeout instead of indefinite wait
            bool workAvailable = cv.wait_for(lock, std::chrono::milliseconds(config.delayPerExec),
                [this] { return finished.load() || !screenQueue.empty(); });

            if (finished.load() && screenQueue.empty()) {
                return;
            }

            if (!screenQueue.empty()) {
                screen = screenQueue.front();
                screenQueue.pop();
                screen->setCoreAssigned(coreId);
                screen->setScheduled(true);
                hasWork = true;
            }
        }

        // CRITICAL: Always record tick activity for this cycle
        auto now = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - lastTickTime);

        if (elapsed.count() >= config.delayPerExec) {
            if (memoryManager) {
                memoryManager->recordCoreActivity(coreId, hasWork, 1);
            }
            lastTickTime = now;
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
        else {
            // CRITICAL: Core is idle - still need to advance time
            std::this_thread::sleep_for(std::chrono::milliseconds(config.delayPerExec));
        }
    }
}

void Scheduler::executeProcessRR(const std::shared_ptr<Screen>& screen, int coreId) {

    static std::mutex console_mutex;


    try {
        screen->setCoreAssigned(coreId);
        int executed = 0;
        int pid = screen->getProcessId();
        int memorySize = screen->getAllocatedMemory();

        if (memoryManager && !memoryManager->isProcessAllocated(pid)) {
            if (memorySize > 0) {
                //std::cout << "[RR] Attempting to allocate " << memorySize
                  //  << " bytes for process " << pid << std::endl;

                if (!memoryManager->allocateProcess(pid, memorySize)) {
                    {
                       std::lock_guard<std::mutex> console_lock(console_mutex);
                       //std::cout << "[RR] Memory allocation failed for process " << screen->getName()
                         // << " - will retry later" << std::endl;
                    }
                    screen->setStatus(ProcessStatus::WAITING);
                    screen->setCoreAssigned(-1);
                    std::this_thread::sleep_for(std::chrono::milliseconds(config.delayPerExec * 5));
                    addProcess(screen);
                    return;
                }
            }
        }

        
        // FIXED: Round Robin should execute for quantum cycles OR until process finishes
       //std::cout << "[RR] Starting quantum for process " << screen->getName()
         //  << " (PID: " << pid << ") - Instructions: " << screen->getCurrentInstruction()
           //  << "/" << screen->getTotalInstructions() << std::endl;

        while (!screen->isFinished() && executed < quantumCycles && !finished.load()) {

            // Handle sleeping processes
            if (screen->getIsSleeping()) {
                bool isExecutingInstruction = false; // Sleeping = not executing
                if (memoryManager) {
                    memoryManager->recordCoreActivity(coreId, isExecutingInstruction, 1);
                }

                screen->decrementSleepTicks();
                if (!screen->checkSleepComplete()) {
                    executed++; // Count sleep ticks towards quantum
                    std::this_thread::sleep_for(std::chrono::milliseconds(config.delayPerExec));
                    continue;
                }
                // If sleep completed, continue to next instruction
            }

            // CRITICAL FIX: Check if process is truly finished BEFORE executing
            if (screen->getCurrentInstruction() >= screen->getTotalInstructions()) {
                screen->setStatus(ProcessStatus::FINISHED);
                // std::cout << "[RR] Process " << screen->getName() << " completed all instructions\n";
                break; // Process is truly finished
            }

            // Execute the next instruction
            bool isExecutingInstruction = true;
            if (memoryManager) {
                memoryManager->recordCoreActivity(coreId, isExecutingInstruction, 1);
            }

            try {
                screen->executeNextInstruction();

                // Check for errors after execution
                if (screen->hasError()) {
                    handleProcessError(screen, "Memory access violation occurred during instruction execution.");
                    break; // Exit on error
                }

                executed++; // Count executed instructions towards quantum
            }
            catch (const std::exception& e) {
                handleProcessError(screen, e.what());
                break; // Exit on exception
            }

            std::this_thread::sleep_for(std::chrono::milliseconds(config.delayPerExec));
        }

        // FIXED: Proper process state management after quantum expires
        if (screen->hasError()) {
            // Process had an error - mark as finished and deallocate
            screen->setStatus(ProcessStatus::FINISHED);
            screen->setCoreAssigned(-1);
            if (memoryManager) {
                memoryManager->deallocateProcess(pid);
            }
            std::cout << "[RR] Process " << screen->getName() << " terminated due to error\n";
        }
        else if (screen->getCurrentInstruction() >= screen->getTotalInstructions()) {
            // Process completed all instructions - mark as finished and deallocate
            screen->setStatus(ProcessStatus::FINISHED);
            screen->setCoreAssigned(-1);
            if (memoryManager) {
                memoryManager->deallocateProcess(pid);
            }
           // std::cout << "[RR] Process " << screen->getName() << " completed ("
            //    << screen->getCurrentInstruction() << "/" << screen->getTotalInstructions() << ")\n";
        }
        else {
            // Quantum expired but process not finished - requeue for next quantum
            screen->setStatus(ProcessStatus::READY);
            screen->setCoreAssigned(-1);
            addProcess(screen); // Put back in ready queue
           // std::cout << "[RR] Process " << screen->getName() << " quantum expired - requeuing ("
            //    << screen->getCurrentInstruction() << "/" << screen->getTotalInstructions()
              //  << ") - executed " << executed << " instructions this quantum\n";
        }
    }
    catch (const std::exception& e) {
        screen->setCoreAssigned(-1);
        handleProcessError(screen, e.what());
        if (memoryManager) {
            memoryManager->deallocateProcess(screen->getProcessId());
        }
    }
}

// SOLUTION 3: Enhanced executeProcessFCFS with consistent tick recording

void Scheduler::executeProcessFCFS(const std::shared_ptr<Screen>& screen, int coreId) {
    try {
        screen->setCoreAssigned(coreId);
        std::ofstream logFile(screen->getName() + ".txt");
        int pid = screen->getProcessId();

        if (memoryManager && !memoryManager->isProcessAllocated(pid)) {
            handleProcessError(screen, "Process not properly allocated in memory");
            return;
        }

        while (!screen->isFinished() && !finished.load()) {
            // CRITICAL: Record tick for every execution cycle
            bool isExecutingInstruction = !screen->getIsSleeping() &&
                (screen->getCurrentInstruction() < screen->getTotalInstructions());

            if (memoryManager) {
                memoryManager->recordCoreActivity(coreId, isExecutingInstruction, 1);
            }

            if (screen->getIsSleeping()) {
                screen->decrementSleepTicks();
                screen->checkSleepComplete();

                if (screen->getIsSleeping()) {
                    // Still sleeping, requeue and exit
                    screen->setStatus(ProcessStatus::READY);
                    screen->setCoreAssigned(-1);
                    addProcess(screen);
                    return;
                }
            }

            if (screen->getCurrentInstruction() >= screen->getTotalInstructions()) {
                processManager->cleanupFinishedProcesses();
                screen->setStatus(ProcessStatus::FINISHED);
                screen->printLog("Process finished execution.");

                if (memoryManager) {
                    memoryManager->deallocateProcess(pid);
                }
                break;
            }

            try {
                auto timestamp = currentTimestamp();
                logFile << timestamp << " Core:" << coreId
                    << " \"Hello world from " << screen->getName() << "!\"\n";
                logFile.flush();

                screen->executeNextInstruction();

                if (screen->hasError()) {
                    handleProcessError(screen, "Memory access violation occurred during instruction execution.");
                    if (memoryManager) {
                        memoryManager->deallocateProcess(pid);
                    }
                    break;
                }
            }
            catch (const std::exception& e) {
                handleProcessError(screen, e.what());
                if (memoryManager) {
                    memoryManager->deallocateProcess(pid);
                }
                break;
            }

            std::this_thread::sleep_for(std::chrono::milliseconds(config.delayPerExec));
        }

        if (!screen->hasError()) {
            screen->setStatus(ProcessStatus::FINISHED);
            processManager->cleanupFinishedProcesses();
            screen->printLog("FCFS: Process completed on core " + std::to_string(coreId));
        }
    }
    catch (const std::exception& e) {
        if (memoryManager) {
            memoryManager->deallocateProcess(screen->getProcessId());
        }
    }
}

void Scheduler::startDummyGeneration() {
    bool expected = false;
    if (!generatingDummies.compare_exchange_strong(expected, true)) {
        /*std::cout << "[Scheduler] Dummy generation already running.\n";*/
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
    /*std::cout << "[Scheduler] Dummy generation stopped.\n";*/
}

void Scheduler::dummyProcessLoop() {
    /*std::cout << "[Scheduler] Dummy process generation started.\n";*/

    try {
        std::random_device rd;
        std::mt19937 gen(rd());
        std::uniform_int_distribution<> dist(config.minIns, config.maxIns);
        // CHANGE: MCO2 - Add memory size distribution for dummy processes
        std::uniform_int_distribution<> memDist(config.minMemPerProc, config.maxMemPerProc);

        dummyCounter = 0;
        auto lastGenTime = std::chrono::steady_clock::now();

        while (generatingDummies.load()) {
            auto now = std::chrono::steady_clock::now();
            auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(now - lastGenTime).count();

            /*if (dummyCounter >= 50) {
                /*std::cout << "[Scheduler] Dummy process limit reached (50). Stopping generation.\n";
                break;
            }*/
            // CHANGE: Check memory pressure before creating new processes
            //if (memoryManager && memoryManager->getUsedMemory() >= (config.maxOverallMem * 0.8)) {
                // Memory is getting full, slow down process creation
               // std::this_thread::sleep_for(std::chrono::milliseconds(config.batchFreq * 2));
                //continue;
            //}

            if (elapsedMs >= config.batchFreq) {
                std::string name = "process" + std::to_string(++dummyCounter);
                /*std::cout << "[Scheduler] Generating dummy process: " << name << " (ID: " << globalProcessId << ")\n";*/

                // CHANGE: MCO2 - Generate dummy processes with random memory sizes
                int memorySize = memDist(gen);
                // Ensure memory size is power of 2
                int powerOf2 = 64; // Start with minimum
                while (powerOf2 < memorySize && powerOf2 < 65536) {
                    powerOf2 *= 2;
                }
                memorySize = powerOf2;

                auto screen = std::make_shared<Screen>();
                screen->setName(name);
                screen->generateDummyInstructions(config);
                screen->setAllocatedMemory(memorySize); // CHANGE: MCO2 - Set memory size for dummy process

                int instructionCount = dist(gen);
                screen->truncateInstructions(instructionCount);
                screen->setProcessId(globalProcessId++);
                screen->setStatus(ProcessStatus::READY);

                // CHANGE: MCO2 - Allocate memory for dummy process
                if (memoryManager && memoryManager->allocateProcess(screen->getProcessId(), memorySize)) {
                    ProcessManager::registerProcess(screen);
                    addProcess(screen);
                    /*std::cout << "[Scheduler] Dummy process " << name << " created with " << memorySize << " bytes\n";*/
                }
                else {
                    /*std::cout << "[Scheduler] Failed to allocate memory for dummy process " << name << "\n";*/
                }

                /* ORIGINAL CODE - commented out
                auto screen = std::make_shared<Screen>();
                screen->setName(name);
                screen->generateDummyInstructions(config);

                int instructionCount = dist(gen);
                screen->truncateInstructions(instructionCount);
                screen->setProcessId(globalProcessId++);
                screen->setStatus(ProcessStatus::READY);
                ProcessManager::registerProcess(screen);
                addProcess(screen);
                */

                lastGenTime = now;
            }
            else {
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
        }
    }
    catch (const std::exception& e) {
        /*std::cerr << "[Scheduler] Exception in dummyProcessLoop: " << e.what() << "\n";*/
    }
    catch (...) {
        /* std::cerr << "[Scheduler] Unknown exception in dummyProcessLoop.\n";*/
    }

    /*std::cout << "[Scheduler] Dummy process generation ended.\n";*/
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
  //  std::cerr << "[Scheduler][ProcessError] Process '" << screen->getName()
    //    << "' encountered an error: " << message << "\n";
}

void Scheduler::startCleanupThread() {
    bool expected = false;
    if (!cleanupRunning.compare_exchange_strong(expected, true)) {
        // Already running
        return;
    }

    if (cleanupThread.joinable()) {
        cleanupThread.join();
    }

    cleanupThread = std::thread(&Scheduler::cleanupLoop, this);
}

void Scheduler::stopCleanupThread() {
    cleanupRunning.store(false);
    if (cleanupThread.joinable()) {
        cleanupThread.join();
    }
}

void Scheduler::cleanupLoop() {
    while (cleanupRunning.load()) {
        if (processManager) {
            processManager->cleanupFinishedProcesses();
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
}