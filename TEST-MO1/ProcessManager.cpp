#include "ProcessManager.h"
#include "Scheduler.h"
#include <iostream>
#include <fstream>
#include <iomanip>
#include <unordered_set>
#include <mutex>
#include <functional>
int globalProcessId = 1;

std::map<std::string, std::shared_ptr<Screen>> ProcessManager::processes;
std::mutex ProcessManager::processMutex;
Scheduler* ProcessManager::scheduler = nullptr;

void ProcessManager::setScheduler(Scheduler* sched) {
    scheduler = sched;
}

void ProcessManager::createAndAttach(const std::string& name, const Config& config) {
    std::vector<Instruction> instructions;
    int numInstructions = rand() % (config.maxIns - config.minIns + 1) + config.minIns;

    std::vector<std::string> variables = { "x", "y", "z", "a", "b", "c" };
    std::unordered_set<std::string> declaredVariables;

    auto generateSimpleInstruction = [&](InstructionType type) -> Instruction {
        Instruction instr;
        instr.type = type;

        switch (type) {
        case InstructionType::DECLARE: {
            std::string var = variables[rand() % variables.size()];
            int value = rand() % 20 + 1;
            instr.args = { var, std::to_string(value) };
            declaredVariables.insert(var);
            break;
        }
        case InstructionType::ADD:
        case InstructionType::SUBTRACT: {
            std::string dest = variables[rand() % variables.size()];
            std::string op1 = variables[rand() % variables.size()];
            std::string op2 = variables[rand() % variables.size()];
            instr.args = { dest, op1, op2 };
            declaredVariables.insert(dest);
            break;
        }
        case InstructionType::PRINT: {
            instr.args = { "Hello from " + name };
            break;
        }
        case InstructionType::SLEEP: {
            instr.args = { std::to_string(rand() % 3 + 1) };
            break;
        }
        default:
            break;
        }

        return instr;
        };

    for (int i = 0; i < numInstructions; ++i) {
        int choice = rand() % 5;  // only 0 to 4
        InstructionType type = static_cast<InstructionType>(choice);
        instructions.push_back(generateSimpleInstruction(type));
    }

    auto screen = std::make_shared<Screen>(name, instructions, globalProcessId++);
    registerProcess(screen);
}


void ProcessManager::resumeScreen(const std::string& name) {
    std::lock_guard<std::mutex> lock(processMutex);
    auto it = processes.find(name);
    if (it != processes.end()) {
        it->second->showScreen();
    }
    else {
        std::cout << "Process \"" << name << "\" not found.\n";
    }
}

void ProcessManager::listScreens(const Config& config) {
    std::lock_guard<std::mutex> lock(processMutex);
    std::cout << "\n----------------------------------------\n";

    std::unordered_set<int> activeCoreIds;
    for (const auto& pair : processes) {
        const std::shared_ptr<Screen>& proc = pair.second;
        if (!proc->isFinished() && proc->getCoreAssigned() != -1) {
            activeCoreIds.insert(proc->getCoreAssigned());
        }
    }


    int totalCores = config.numCpu;
    int activeCores = static_cast<int>(activeCoreIds.size());
    int coresAvailable = std::max(0, totalCores - activeCores);
    double utilization = (static_cast<double>(activeCores) / totalCores) * 100.0;

    std::cout << "CPU Stats:\n"
        << "Cores Used:      " << activeCores << " / " << totalCores << "\n"
        << "Cores Available: " << coresAvailable << "\n"
        << "CPU Utilization: " << std::fixed << std::setprecision(2) << utilization << "%\n"
        << "\n----------------------------------------\n";

    std::cout << "\nRunning Processes:\n";
    int cntRunning = 0;
    for (const auto& pair : processes) {
        const std::string& name = pair.first;
        const std::shared_ptr<Screen>& proc = pair.second;

        if (proc->getStatus() == ProcessStatus::RUNNING) {
            cntRunning++;
            std::cout << std::setw(15) << std::left << ("- " + name)
                << std::setw(22) << ("(" + proc->getCreationTimestamp() + ")")
                << "Core: " << std::setw(3) << proc->getCoreAssigned()
                << "   " << proc->getCurrentInstruction()
                << " / " << proc->getTotalInstructions() << "\n";
        }
    }

    if (cntRunning == 0) std::cout << "No running processes.\n";

    if (cntRunning == 0) std::cout << "No running processes.\n";

    std::cout << "\nFinished Processes:\n";
    int cntFinished = 0;
    for (const auto& pair : processes) {
        const std::string& name = pair.first;
        const std::shared_ptr<Screen>& proc = pair.second;

        if (proc->isFinished()) {
            cntFinished++;
            std::cout << std::setw(15) << std::left << ("- " + name)
                << std::setw(22) << ("(" + proc->getCreationTimestamp() + ")")
                << "Finished   "
                << proc->getTotalInstructions() << " / " << proc->getTotalInstructions() << "\n";
        }
    }
    if (cntFinished == 0) std::cout << "No finished processes.\n";

    std::cout << "----------------------------------------\n\n";
}

std::vector<std::shared_ptr<Screen>> ProcessManager::getAllProcesses() {
    std::lock_guard<std::mutex> lock(processMutex);
    std::vector<std::shared_ptr<Screen>> all;
    for (const auto& p : processes) {
        all.push_back(p.second);
    }
    return all;
}

void ProcessManager::generateReport() {
    std::ofstream file("csopesy-log.txt");
    if (!file.is_open()) {
        std::cerr << "Failed to open csopesy-log.txt for writing.\n";
        return;
    }

    std::vector<std::shared_ptr<Screen>> running, finished;
    int totalCores = 4;
    int coresUsed = 0;

    {
        std::lock_guard<std::mutex> lock(processMutex);
        for (const auto& pair : processes) {
            const std::shared_ptr<Screen>& proc = pair.second;

            if (proc->isFinished()) {
                finished.push_back(proc);
            }
            else {
                running.push_back(proc);
                if (proc->getCoreAssigned() >= 0) ++coresUsed;
            }
        }

    }

    float cpuUtil = totalCores == 0 ? 0.0f : (static_cast<float>(coresUsed) / totalCores) * 100.0f;

    file << "----------------------------------------\n"
        << "CPU Stats:\n"
        << "Cores Used:      " << coresUsed << " / " << totalCores << "\n"
        << "Cores Available: " << (totalCores - coresUsed) << "\n"
        << "CPU Utilization: " << std::fixed << std::setprecision(2) << cpuUtil << "%\n"
        << "----------------------------------------\n\n";

    file << "Running Processes:\n";
    for (const auto& proc : running) {
        file << std::left << std::setw(15) << ("- " + proc->getName())
            << std::setw(22) << ("(" + proc->getCreationTimestamp() + ")")
            << "Core: " << std::setw(2) << proc->getCoreAssigned()
            << "  " << proc->getCurrentInstruction()
            << " / " << proc->getTotalInstructions() << "\n";
    }

    file << "\nFinished Processes:\n";
    for (const auto& proc : finished) {
        file << std::left << std::setw(15) << ("- " + proc->getName())
            << std::setw(22) << ("(" + proc->getCreationTimestamp() + ")")
            << "Finished  "
            << proc->getTotalInstructions() << " / " << proc->getTotalInstructions() << "\n";
    }

    file << "----------------------------------------\n";
    std::cout << "Report saved to csopesy-log.txt\n";
}

void ProcessManager::registerProcess(std::shared_ptr<Screen> process) {
    {
        std::lock_guard<std::mutex> lock(processMutex);
        processes[process->getName()] = process;
    }

    if (scheduler) {
        scheduler->addProcess(process);
    }
}

bool ProcessManager::hasProcess(const std::string& name) {
    std::lock_guard<std::mutex> lock(processMutex);
    return processes.find(name) != processes.end();
}

std::shared_ptr<Screen> ProcessManager::getProcess(const std::string& name) {
    std::lock_guard<std::mutex> lock(processMutex);
    auto it = processes.find(name);
    return it != processes.end() ? it->second : nullptr;
}
