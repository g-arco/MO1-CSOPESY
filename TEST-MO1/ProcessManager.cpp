#include "ProcessManager.h"
#include "Scheduler.h"
#include <iostream>
#include <fstream>
#include <iomanip>
#include <unordered_set>
#include <mutex>
#include <functional>
#include <set>
#include <string>



int globalProcessId = 1;

std::map<std::string, std::shared_ptr<Screen>> ProcessManager::processes;
std::mutex ProcessManager::processMutex;
Scheduler* ProcessManager::scheduler = nullptr;

void ProcessManager::setScheduler(Scheduler* sched) {
    scheduler = sched;
}

void ProcessManager::createAndAttach(const std::string& name, const Config& config) {
    std::vector<Instruction> instructions;
    std::vector<std::string> variables = { "x", "y", "z", "a", "b", "c" };
    std::set<std::string> declaredVars;

    int totalInstructions = 0;
    const int minDeclare = 3;

    // Seed random
    srand((unsigned int)time(nullptr));

    // Ensure at least 3 DECLARE instructions
    for (int i = 0; i < minDeclare && totalInstructions < config.maxIns; ++i) {
        std::string var = variables[rand() % variables.size()];
        int value = rand() % 20 + 1;

        Instruction declare;
        declare.type = InstructionType::DECLARE;
        declare.args = { var, std::to_string(value) };

        declaredVars.insert(var);
        instructions.push_back(declare);
        totalInstructions++;
    }

    // Declare lambdas
    std::function<Instruction(int, int&)> generateForInstruction;
    std::function<Instruction(InstructionType,
        std::set<std::string>&,
        const std::vector<std::string>&,
        const std::string&,
        int&,
        int)> generateSimpleInstruction;

    // Define generateSimpleInstruction first
    generateSimpleInstruction = [&](InstructionType type, std::set<std::string>& declared, const std::vector<std::string>& vars,
        const std::string& procName, int& budget, int depth = 1) -> Instruction {
            Instruction instr;
            instr.type = type;

            switch (type) {
            case InstructionType::DECLARE: {
                std::string var = vars[rand() % vars.size()];
                int value = rand() % 20 + 1;
                declared.insert(var);
                instr.args = { var, std::to_string(value) };
                break;
            }
            case InstructionType::ADD:
            case InstructionType::SUBTRACT: {
                if (declared.size() < 1) break;

                std::string dest = vars[rand() % vars.size()];
                std::string op1 = *std::next(declared.begin(), rand() % declared.size());
                std::string op2 = (rand() % 2 == 0)
                    ? *std::next(declared.begin(), rand() % declared.size())
                    : std::to_string(rand() % 20);

                declared.insert(dest);
                instr.args = { dest, op1, op2 };
                break;
            }
            case InstructionType::PRINT: {
                instr.args = { "Hello world from " + procName + "!" };
                break;
            }
            case InstructionType::SLEEP: {
                instr.args = { std::to_string(rand() % 5 + 1) };
                break;
            }
            case InstructionType::FOR: {
                if (depth < 3) {
                    instr = generateForInstruction(depth, budget);
                }
                break;
            }
            default:
                break;
            }
            return instr;
        };

    // Now define generateForInstruction, which calls generateSimpleInstruction
    generateForInstruction = [&](int depth, int& remaining) -> Instruction {
        Instruction forInstr;
        forInstr.type = InstructionType::FOR;

        int repeatCount = rand() % 5 + 1;
        int subCount = rand() % 3 + 1;

        std::vector<std::string> args;

        for (int i = 0; i < subCount && remaining > 0; ++i) {
            int choice = rand() % 6;

            if (choice == static_cast<int>(InstructionType::FOR) && depth < 3) {
                Instruction nested = generateForInstruction(depth + 1, remaining);
                args.push_back("FOR");
                args.insert(args.end(), nested.args.begin(), nested.args.end());
            }
            else {
                Instruction inner = generateSimpleInstruction(static_cast<InstructionType>(choice), declaredVars, variables, name, remaining, depth);
                if (!inner.args.empty()) {
                    args.push_back(std::to_string(static_cast<int>(inner.type)));
                    args.insert(args.end(), inner.args.begin(), inner.args.end());
                    args.push_back(";");
                    remaining--;
                }
            }
        }

        args.push_back("REP");
        args.push_back(std::to_string(repeatCount));
        forInstr.args = args;
        return forInstr;
        };

    // Generate remaining instructions
    while (totalInstructions < config.maxIns) {
        int choice = rand() % 6;
        InstructionType type = static_cast<InstructionType>(choice);

        Instruction instr = generateSimpleInstruction(type, declaredVars, variables, name, totalInstructions, 1);
        if (!instr.args.empty()) {
            instructions.push_back(instr);
            if (type != InstructionType::FOR) {
                totalInstructions++;
            }
        }
    }

    auto screen = std::make_shared<Screen>(name, instructions, globalProcessId++);
    registerProcess(screen);
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

        if (!proc->isFinished() && proc->getCurrentInstruction() > 0 && proc->getCoreAssigned() != -1) {
            cntRunning++;
            std::cout << std::setw(15) << std::left << ("- " + name)
                << std::setw(22) << ("(" + proc->getCreationTimestamp() + ")")
                << "Core: " << std::setw(3) << proc->getCoreAssigned()
                << "   " << proc->getCurrentInstruction()
                << " / " << proc->getTotalInstructions() << "\n";
        }
    }

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
