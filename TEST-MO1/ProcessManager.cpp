#include "ProcessManager.h"
#include <iostream>
#include <fstream>
#include <iomanip>
#include <unordered_set>

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

void ProcessManager::listScreens(const Config& config) {
    std::cout << "\n----------------------------------------\n";

    std::unordered_set<int> activeCoreIds;

    // Count active cores
    for (const auto& [_, proc] : processes) {
        if (!proc->isFinished() && proc->getCoreAssigned() != -1) {
            activeCoreIds.insert(proc->getCoreAssigned());
        }
    }

    int totalCores = config.numCpu;
    int activeCores = activeCoreIds.size();
    int coresAvailable = std::max(0, totalCores - activeCores);
    double utilization = (static_cast<double>(activeCores) / totalCores) * 100.0;

    // CPU STATS
    std::cout << "CPU Stats:\n";
    std::cout << "Cores Used:      " << activeCores << " / " << totalCores << "\n";
    std::cout << "Cores Available: " << coresAvailable << "\n";
    std::cout << "CPU Utilization: " << std::fixed << std::setprecision(2) << utilization << "%\n";

    // RUNNING PROCESSES
     std::cout << "\n----------------------------------------\n";
    std::cout << "\nRunning Processes:\n";
    int cntRunning = 0;
    for (const auto& [name, proc] : processes) {
        if (!proc->isFinished() && proc->getCurrentInstruction() > 0) {
            cntRunning++;
            std::cout << std::setw(15) << std::left << ("- " + name)
                      << std::setw(22) << ("(" + proc->getCreationTimestamp() + ")")
                      << "Core: " << std::setw(3) << proc->getCoreAssigned()
                      << "   " << proc->getCurrentInstruction()
                      << " / " << proc->getTotalInstructions() << "\n";
        }
    }
    if (cntRunning == 0) {
        std::cout << "No running processes.\n";
    }

    // FINISHED PROCESSES
    std::cout << "\nFinished Processes:\n";
    int cntFinished = 0;
    for (const auto& [name, proc] : processes) {
        if (proc->isFinished()) {
            cntFinished++;
            std::cout << std::setw(15) << std::left << ("- " + name)
                      << std::setw(22) << ("(" + proc->getCreationTimestamp() + ")")
                      << "Finished   "
                      << proc->getTotalInstructions() << " / " << proc->getTotalInstructions() << "\n";
        }
    }
    if (cntFinished == 0) {
        std::cout << "No finished processes.\n";
    }

    std::cout << "----------------------------------------\n\n";
}




void ProcessManager::generateReport() {
    std::ofstream file("csopesy-log.txt");
    if (!file.is_open()) {
        std::cerr << "Failed to open csopesy-log.txt for writing.\n";
        return;
    }

    const auto& procs = getProcesses();
    int totalCores = 4;
    int coresUsed = 0;

    std::vector<std::shared_ptr<Screen>> running, finished;
    for (const auto& pair : procs) {
        auto proc = pair.second;
        if (proc->isFinished()) {
            finished.push_back(proc);
        } else {
            running.push_back(proc);
            if (proc->getCoreAssigned() >= 0) ++coresUsed;
        }
    }

    float cpuUtil = totalCores == 0 ? 0 : (float(coresUsed) / totalCores) * 100;

    file << "----------------------------------------\n";
    file << "CPU Stats:\n";
    file << "Cores Used:      " << coresUsed << " / " << totalCores << "\n";
    file << "Cores Available: " << (totalCores - coresUsed) << "\n";
    file << "CPU Utilization: " << std::fixed << std::setprecision(2) << cpuUtil << "%\n";
    file << "----------------------------------------\n\n";

    file << "Running Processes:\n";
    for (auto& proc : running) {
        file << std::left << std::setw(15) << ("- " + proc->getName())
             << std::setw(22) << ("(" + proc->getCreationTimestamp() + ")")
             << "Core: " << std::setw(2) << proc->getCoreAssigned()
             << "  " << proc->getCurrentInstruction()
             << " / " << proc->getTotalInstructions() << "\n";
    }

    file << "\nFinished Processes:\n";
    for (auto& proc : finished) {
        file << std::left << std::setw(15) << ("- " + proc->getName())
             << std::setw(22) << ("(" + proc->getCreationTimestamp() + ")")
             << "Finished  "
             << proc->getTotalInstructions() << " / " << proc->getTotalInstructions() << "\n";
    }

    file << "----------------------------------------\n";
    file.close();
    std::cout << "Report saved to csopesy-log.txt\n";
}


void ProcessManager::registerProcess(std::shared_ptr<Screen> process) {
    processes[process->getName()] = process;
}