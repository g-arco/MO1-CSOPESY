#include "Scheduler.h"
#include "ProcessManager.h"
#include "Config.h"
#include "CLIUtils.h"

#include <iostream>
#include <sstream>
#include <thread>
#include <string>

bool initialized = false;
Scheduler* scheduler = nullptr;

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
            std::cout << "Exiting...\n";
            break;
        }

        if (cmd == "initialize") {
            if (scheduler) {
                scheduler->finish();
                delete scheduler;
                scheduler = nullptr;
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

                scheduler = new Scheduler(config);
                initialized = true;

                std::cout << "System initialized successfully.\n\n";
            } catch (const std::exception& e) {
                std::cerr << "Failed to initialize system: " << e.what() << "\n";
            }
        }
        else if (!initialized) {
            std::cout << "Command not available. Please run 'initialize' first.\n";
        }
        else if (cmd == "scheduler-start") {
            if (!scheduler) {
                std::cout << "System not initialized. Use `initialize` first.\n";
            } else {
                scheduler->start();

                auto allProcs = ProcessManager::getAllProcesses(); // Ensure this returns references or pointers
                for (const auto& proc : allProcs) {
                    if (proc->getStatus() == ProcessStatus::READY) {
                        proc->setStatus(ProcessStatus::RUNNING);
                    }
                }

                scheduler->startDummyGeneration();
            }
        }
        else if (cmd == "scheduler-stop") {
            if (scheduler) {
                scheduler->stopDummyGeneration();
                scheduler->finish();
            }
        }
        else if (cmd == "screen") {
            std::string opt;
            iss >> opt;

            if (opt == "-s") {
                std::string name;
                iss >> name;

                if (name.empty()) {
                    std::cout << "Please provide a screen name.\n";
                } else {
                    if (ProcessManager::hasProcess(name)) {
                        std::cout << "Screen with name '" << name << "' already exists. Use 'screen -r " << name << "' to resume.\n";
                    } else {
                        ProcessManager::createAndAttach(name, config);
                        auto proc = ProcessManager::getProcess(name);
                        scheduler->addProcess(proc);
                        std::cout << "[Main] Screen '" << name << "' added to scheduler queue.\n";
                        proc->showScreen();
                    }
                }
            }
            else if (opt == "-r") {
                std::string name;
                iss >> name;

                if (name.empty()) {
                    std::cout << "Please specify a screen name to resume.\n";
                } else if (!ProcessManager::getProcess(name)) {
                    std::cout << "No screen found with the name '" << name << "'.\n";
                } else {
                    ProcessManager::getProcess(name)->showScreen();
                }
            }
            else if (opt == "-ls") {
                ProcessManager::listScreens(config);
            }
            else {
                std::cout << "Unknown screen option.\n";
            }
        }
        else if (cmd == "report-util") {
            ProcessManager::generateReport();
        }
        else {
            std::cout << "Unrecognized command.\n";
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
