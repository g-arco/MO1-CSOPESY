#include "Scheduler.h"
#include "ProcessManager.h"
#include "Config.h"
#include "CLIUtils.h"
#include <iostream>
#include <sstream>
#include <string>

bool initialized = false;
Config config;
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
            config = Config::load("config.txt");
            scheduler = new Scheduler(config);
            initialized = true;
            std::cout << "System initialized.\n";
        }
        else if (!initialized) {
            std::cout << "Command not available. Please run 'initialize' first.\n";
        }
        else if (cmd == "scheduler-start") {
            if (!scheduler) {
                std::cout << "System not initialized. Use `initialize` first.\n";
            }
            else {
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
                iss >> name;

                if (name.empty()) {
                    std::cout << "Please provide a screen name.\n";
                }
                else {
                    if (ProcessManager::getProcesses().count(name)) {
                        std::cout << "Screen with name '" << name << "' already exists. Use 'screen -r " << name << "' to resume.\n";
                    }
                    else {
                        ProcessManager::createAndAttach(name, config);

                        //  ADD the new screen process to the scheduler
                        auto proc = ProcessManager::getProcesses().at(name);
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
                }
                else if (!ProcessManager::getProcesses().count(name)) {
                    std::cout << "No screen found with the name '" << name << "'.\n";
                }
                else {
                    ProcessManager::getProcesses().at(name)->showScreen();
                }
            }
            else if (opt == "-ls") {
                ProcessManager::listScreens();
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
    delete scheduler;
    return 0;
}
