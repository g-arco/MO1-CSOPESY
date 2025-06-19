#include "ProcessManager.h"
#include <iostream>
#include <fstream>
#include <iomanip>

std::map<std::string, std::shared_ptr<Screen>> ProcessManager::processes;

void ProcessManager::createAndAttach(const std::string& name, const Config& config) {
    std::vector<Instruction> instructions = {
        { InstructionType::PRINT, { "Hello world from " + name + "!" } }
    };
    auto screen = std::make_shared<Screen>(name, instructions);
    registerProcess(screen);
    screen->showScreen();
}

void ProcessManager::resumeScreen(const std::string& name) {
    auto it = processes.find(name);
    if (it != processes.end()) {
        it->second->showScreen();
    }
    else {
        std::cout << "Process " << name << " not found.\n";
    }
}

void ProcessManager::listScreens() {
    std::cout << "Running Processes:\n";

    for (auto it = processes.begin(); it != processes.end(); ++it) {
        auto& proc = it->second;
        if (!proc->isFinished()) {
            std::cout << "- " << proc->getName()
                << " (" << proc->getCreationTimestamp() << ")  "
                << "Core " << proc->getCoreAssigned() << "  "
                << proc->getCurrentInstruction() << "/" << proc->getTotalInstructions() << "\n";
        }
    }

    std::cout << "\nFinished Processes:\n";

    for (auto it = processes.begin(); it != processes.end(); ++it) {
        auto& proc = it->second;
        if (proc->isFinished()) {
            std::cout << "- " << proc->getName()
                << " (" << proc->getCreationTimestamp() << ")  "
                << "Finished  "
                << proc->getTotalInstructions() << "/" << proc->getTotalInstructions() << "\n";
        }
    }
}


void ProcessManager::generateReport() {
    std::ofstream file("csopesy-log.txt");
    if (!file.is_open()) {
        std::cerr << "Failed to open csopesy-log.txt for writing.\n";
        return;
    }

    const auto& procs = getProcesses();
    int totalCores = 4; // Can be fetched from config if passed
    int coresUsed = 0;

    std::vector<std::shared_ptr<Screen>> running, finished;
    for (const auto& pair : procs) {
        auto proc = pair.second;

        if (proc->isFinished()) {
            finished.push_back(proc);
        }
        else {
            running.push_back(proc);
            if (proc->getCoreAssigned() >= 0) ++coresUsed;
        }
    }

    float cpuUtil = totalCores == 0 ? 0 : (float(coresUsed) / totalCores) * 100;

    file << "CPU Util: " << int(cpuUtil) << "%\n";
    file << "Core used: " << coresUsed << "\n";
    file << "Core avail: " << totalCores << "\n\n";

    file << "Running Processes:\n";
    for (auto& proc : running) {
        file << proc->getName()
            << " (" << proc->getCreationTimestamp() << ")  "
            << "Core " << proc->getCoreAssigned() << "   "
            << proc->getCurrentInstruction() << "/" << proc->getTotalInstructions() << "\n";
    }

    file << "\nFinished Processes:\n";
    for (auto& proc : finished) {
        file << proc->getName()
            << " (" << proc->getCreationTimestamp() << ")  "
            << "Finished   "
            << proc->getTotalInstructions() << "/" << proc->getTotalInstructions() << "\n";
    }

    file.close();
    std::cout << "Report saved to csopesy-log.txt\n";
}

void ProcessManager::registerProcess(std::shared_ptr<Screen> process) {
    processes[process->getName()] = process;
}