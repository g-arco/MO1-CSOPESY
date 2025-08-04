#include "Scheduler.h"
#include "ProcessManager.h"
#include "Config.h"
#include "CLIUtils.h"
#include "MemoryManager.h"

#include <iostream>
#include <sstream>
#include <thread>
#include <string>
#include <memory>

bool initialized = false;
Scheduler* scheduler = nullptr;

// The global MemoryManager object is DEFINED here.
// This is the single place where the variable is created.
std::unique_ptr<MemoryManager> memoryManager;
std::shared_ptr<ProcessManager> processManager;


void commandLoop() {
    std::string input;
    CLIUtils::clearScreen();
    CLIUtils::printHeader();

    while (true) {
        std::cout << "\033[1;32mlinux@ubuntu\033[0m:\033[1;34m~\033[0m$ ";
        std::getline(std::cin, input);

        std::istringstream iss(input);
        std::string cmd;
        iss >> cmd;

        if (cmd == "exit") {
            if (scheduler) {
                scheduler->finish();
            }
            std::cout << "Exiting...\n";
            break;
        }

        if (cmd == "initialize") {
            if (scheduler) {
                scheduler->finish();
                delete scheduler;
                scheduler = nullptr;
            }

            if (memoryManager != nullptr) {
                memoryManager->finish();
                memoryManager.reset(); // Release the unique_ptr
            }


            try {
                config.loadConfig("config.txt");

                std::cout << "\nConfiguration Loaded:\n";
                std::cout << "Number of CPUs: " << config.numCpu << "\n";
                std::cout << "Scheduler: " << config.schedulerType << "\n";
                std::cout << "Quantum Cycles: " << config.quantum << "\n";
                std::cout << "Batch Process Frequency: " << config.batchFreq << "\n";
                std::cout << "Minimum Instructions: " << config.minIns << "\n";
                std::cout << "Maximum Instructions: " << config.maxIns << "\n";
                std::cout << "Delays per Exec: " << config.delayPerExec << "\n";
                std::cout << "Maximum Overall Memory: " << config.maxOverallMem << "\n";
                std::cout << "Memory per Frame: " << config.memPerFrame << "\n";
                std::cout << "Minimum Memory per Process: " << config.minMemPerProc << "\n";
                std::cout << "Maximum Memory per Process: " << config.maxMemPerProc << "\n";

                // Use the reset() method to safely assign a new object to the unique_ptr.
                // This is the correct way to handle unique_ptr assignment.
                scheduler = new Scheduler(config);
                processManager = std::make_shared<ProcessManager>();
                ProcessManager::setScheduler(scheduler); // CHANGE: Set scheduler in ProcessManager
                memoryManager = std::make_unique<MemoryManager>(config.maxOverallMem, config.minMemPerProc, config.maxMemPerProc, config.memPerFrame);

                initialized = true;

                std::cout << "System initialized successfully.\n\n";
            }
            catch (const std::exception& e) {
                std::cerr << "Failed to initialize system: " << e.what() << "\n";
            }
        }
        else if (!initialized) {
            std::cout << "Command not available. Please run 'initialize' first.\n";
        }
        else if (cmd == "scheduler-start") {
            if (!scheduler) {
                std::cout << "System not initialized. Use `initialize` first.\n";
            }
            else {
                scheduler->start();
                scheduler->startCleanupThread();
                scheduler->startDummyGeneration();
            }
        }
        else if (cmd == "scheduler-stop") {
            if (scheduler) {
                scheduler->stopDummyGeneration();
            }
        }
        else if (cmd == "screen") {
            std::string opt;
            iss >> opt;

            if (opt == "-s") {
                std::string name;
                // CHANGE: MCO2 - Modified screen -s to support memory allocation
                std::string memoryStr;
                iss >> name >> memoryStr;

                if (name.empty()) {
                    std::cout << "Please provide a screen name.\n";
                }
                else if (memoryStr.empty()) {
                    // CHANGE: MCO2 - Memory size is now required for screen -s
                    std::cout << "Usage: screen -s <process_name> <process_memory_size>\n";
                    std::cout << "Memory size must be between 64 and 65536 bytes and a power of 2.\n";
                }
                else {
                    if (ProcessManager::hasProcess(name)) {
                        std::cout << "Screen with name '" << name << "' already exists. Use 'screen -r " << name << "' to resume.\n";
                    }
                    else {
                        try {
                            int memorySize = std::stoi(memoryStr);
                            // CHANGE: MCO2 - Use new method with memory allocation
                            ProcessManager::createProcessWithMemory(name, memorySize, config);
                            auto proc = ProcessManager::getProcess(name);
                            if (proc) {
                                scheduler->addProcess(proc);
                                std::cout << "[Main] Screen '" << name << "' added to scheduler queue.\n";
                                proc->showScreen();
                            }
                        }
                        catch (const std::exception& e) {
                            std::cout << "Invalid memory size: " << memoryStr << "\n";
                        }
                    }
                }
            }
            // CHANGE: MCO2 - New screen -c command for custom instructions
            else if (opt == "-c") {
                std::string name, memoryStr;
                iss >> name >> memoryStr;

                if (name.empty() || memoryStr.empty()) {
                    std::cout << "Usage: screen -c <process_name> <process_memory_size> \"<instructions>\"\n";
                    std::cout << "Instructions should be semicolon-separated and enclosed in quotes.\n";
                    return;
                }

                // Read the rest of the line for instructions
                std::string instructionPart;
                std::getline(iss, instructionPart);

                // Find the quoted instruction string
                size_t start = instructionPart.find('"');
                size_t end = instructionPart.rfind('"');

                if (start == std::string::npos || end == std::string::npos || start >= end) {
                    std::cout << "Instructions must be enclosed in double quotes.\n";
                    return;
                }

                std::string instructions = instructionPart.substr(start + 1, end - start - 1);

                if (ProcessManager::hasProcess(name)) {
                    std::cout << "Screen with name '" << name << "' already exists.\n";
                }
                else {
                    try {
                        int memorySize = std::stoi(memoryStr);
                        ProcessManager::createProcessWithInstructions(name, memorySize, instructions, config);
                        auto proc = ProcessManager::getProcess(name);
                        if (proc) {
                            scheduler->addProcess(proc);
                            std::cout << "[Main] Screen '" << name << "' with custom instructions added to scheduler queue.\n";
                            proc->showScreen();
                        }
                    }
                    catch (const std::exception& e) {
                        std::cout << "Invalid memory size: " << memoryStr << "\n";
                    }
                }
            }
            else if (opt == "-r") {
                std::string name;
                iss >> name;

                if (name.empty()) {
                    std::cout << "Please specify a screen name to resume.\n";
                }
                else {
                    auto proc = ProcessManager::getProcess(name);
                    if (!proc) {
                        std::cout << "Process " << name << " not found.\n";
                    }
                    else {
                        // CHANGE: MCO2 - Enhanced screen -r to show memory violation errors
                        if (memoryManager && memoryManager->hasProcessViolation(proc->getProcessId())) {
                            std::string violationInfo = memoryManager->getViolationInfo(proc->getProcessId());
                            std::cout << "Process " << name << " shut down due to memory access violation error that occurred at "
                                << violationInfo << "\n";
                        }
                        else {
                            proc->showScreen();
                        }
                    }
                }
            }
            else if (opt == "-ls") {
                ProcessManager::listScreens(config);
            }
            else {
                std::cout << "Unknown screen option. Available options: -s, -c, -r, -ls\n";
            }
        }
        // CHANGE: MCO2 - New process-smi command
        else if (cmd == "process-smi") {
            if (memoryManager) {
                memoryManager->processingSmi();
            }
            else {
                std::cout << "Memory manager not initialized. Run 'initialize' first.\n";
            }
        }
        // CHANGE: MCO2 - New vmstat command
        else if (cmd == "vmstat") {
            if (memoryManager) {
                memoryManager->vmstat(processManager->getCpuUtilization(config.numCpu));
            }
            else {
                std::cout << "Memory manager not initialized. Run 'initialize' first.\n";
            }
        }
        // ORIGINAL COMMANDS (kept for compatibility)
        else if (cmd == "report-util") {
            ProcessManager::generateReport();
        }
        // ORIGINAL COMMANDS (commented out - replaced by new memory management)
        /*
        else if (cmd == "process-smi") {
            ProcessManager::smiReport();
        }
        else if (cmd == "vmstat") {
            ProcessManager::vmstatReport();
        }
        */
        else if (cmd == "swap-out") {
            std::string name;
            iss >> name;
            if (name.empty()) {
                std::cout << "Please specify a process name to swap out.\n";
            }
            else {
                ProcessManager::swapOut(name);
            }
        }
        else if (cmd == "swap-in") {
            std::string name;
            iss >> name;
            if (name.empty()) {
                std::cout << "Please specify a process name to swap in.\n";
            }
            else {
                ProcessManager::swapIn(name);
            }
        }
        else {
            std::cout << "Unrecognized command.\n";
            std::cout << "Available commands:\n";
            std::cout << "  initialize - Initialize the system\n";
            std::cout << "  scheduler-start - Start the scheduler\n";
            std::cout << "  scheduler-stop - Stop the scheduler\n";
            std::cout << "  screen -s <name> <memory_size> - Create process with memory\n";
            std::cout << "  screen -c <name> <memory_size> \"<instructions>\" - Create process with custom instructions\n";
            std::cout << "  screen -r <name> - Resume/view process\n";
            std::cout << "  screen -ls - List all processes\n";
            std::cout << "  process-smi - Show memory overview\n";
            std::cout << "  vmstat - Show detailed memory statistics\n";
            std::cout << "  report-util - Generate utilization report\n";
            std::cout << "  exit - Exit the program\n";
        }
    }
}

int main() {
    commandLoop();

    if (scheduler) {
        delete scheduler;
        scheduler = nullptr;
    }

    return 0;
}