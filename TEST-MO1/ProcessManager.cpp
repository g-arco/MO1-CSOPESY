#include "ProcessManager.h"
#include "Scheduler.h"
#include "MemoryManager.h"  // CHANGE: Include for memory operations

#include <iostream>
#include <fstream>
#include <iomanip>
#include <optional>
#include <unordered_set>
#include <mutex>
#include <functional>
#include <sstream>

// CHANGE: Include external memory manager
extern std::unique_ptr<MemoryManager> memoryManager;

int globalProcessId = 1;

std::map<std::string, std::shared_ptr<Screen>> ProcessManager::processes;
std::mutex ProcessManager::processMutex;
Scheduler* ProcessManager::scheduler = nullptr;

void ProcessManager::setScheduler(Scheduler* sched) {
    scheduler = sched;
}

// CHANGE: Enhanced createAndAttach to support memory allocation
void ProcessManager::createAndAttach(const std::string& name, const Config& config) {
    std::vector<Instruction> instructions;
    int numInstructions = rand() % (config.maxIns - config.minIns + 1) + config.minIns;

    std::vector<std::string> allVariables = { "x", "y", "z", "a", "b", "c" };
    std::unordered_set<std::string> declaredVariables;

    auto getRandomDeclaredVar = [&]() -> std::string {
        if (declaredVariables.empty()) return "";
        auto it = declaredVariables.begin();
        std::advance(it, rand() % declaredVariables.size());
        return *it;
        };

    auto getUniqueVarForDeclare = [&]() -> std::string {
        for (const auto& var : allVariables) {
            if (declaredVariables.find(var) == declaredVariables.end())
                return var;
        }
        // Fallback if all are declared
        return "v" + std::to_string(rand() % 1000);
        };

    auto generateSimpleInstruction = [&](InstructionType type) -> Instruction {
        Instruction instr;
        instr.type = type;

        switch (type) {
        case InstructionType::DECLARE: {
            std::string var = getUniqueVarForDeclare();
            int value = rand() % 20 + 1;
            instr.args = { var, std::to_string(value) };
            declaredVariables.insert(var);
            break;
        }
        case InstructionType::ADD:
        case InstructionType::SUBTRACT: {
            std::string dest = getRandomDeclaredVar();
            std::string op1 = getRandomDeclaredVar();
            std::string op2 = getRandomDeclaredVar();
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
        case InstructionType::READ: {
            std::string var = getRandomDeclaredVar();
            uint32_t addr = rand() % config.minMemPerProc;
            std::ostringstream oss;
            oss << "0x" << std::hex << addr;
            instr.args = { var, oss.str() };
            break;
        }
        case InstructionType::WRITE: {
            std::string var = getRandomDeclaredVar();
            uint32_t addr = rand() % config.minMemPerProc;
            std::ostringstream oss;
            oss << "0x" << std::hex << addr;
            int value = rand() % 100 + 1;
            instr.args = { oss.str(), std::to_string(value) };
            break;
        }
        default:
            break;
        }  // <-- end switch

        return instr;  // return *after* the switch block
        };  // <-- end lambda
     

    int numDeclares = std::min<int>(rand() % 3 + 1, allVariables.size());
    for (int i = 0; i < numDeclares; ++i) {
        Instruction declInstr = generateSimpleInstruction(InstructionType::DECLARE);
        instructions.push_back(declInstr);
    }

    while (instructions.size() < static_cast<size_t>(numInstructions)) {
        int choice = rand() % 7;
        InstructionType type = static_cast<InstructionType>(choice);
        Instruction instr = generateSimpleInstruction(type);
        instructions.push_back(instr);
    }


    auto screen = std::make_shared<Screen>(name, instructions, globalProcessId++);
    registerProcess(screen);
}

// CHANGE: New method to create process with specific memory size
void ProcessManager::createProcessWithMemory(const std::string& name, int memorySize, const Config& config) {
    // Validate memory size
    if (memorySize < 64 || memorySize > 65536) {
        std::cout << "Invalid memory allocation: size must be between 64 and 65536 bytes\n";
        return;
    }

    if ((memorySize & (memorySize - 1)) != 0) {
        std::cout << "Invalid memory allocation: size must be a power of 2\n";
        return;
    }

    std::vector<Instruction> instructions;
    auto screen = std::make_shared<Screen>(name, instructions, globalProcessId++);
    screen->setAllocatedMemory(memorySize);

    // Allocate memory through memory manager
    if (memoryManager && memoryManager->allocateProcess(screen->getProcessId(), memorySize)) {
        registerProcess(screen);
        std::cout << "Process '" << name << "' created with " << memorySize << " bytes allocated.\n";
    }
    else {
        std::cout << "Failed to allocate memory for process '" << name << "'.\n";
    }
}

// CHANGE: New method to create process with custom instructions
void ProcessManager::createProcessWithInstructions(const std::string& name, int memorySize, const std::string& instructionString, const Config& config) {
    // Validate memory size
    if (memorySize < 64 || memorySize > 65536) {
        std::cout << "Invalid memory allocation: size must be between 64 and 65536 bytes\n";
        return;
    }

    if ((memorySize & (memorySize - 1)) != 0) {
        std::cout << "Invalid memory allocation: size must be a power of 2\n";
        return;
    }

    // Parse instruction string
    std::vector<Instruction> instructions = parseInstructionString(instructionString);

    if (instructions.size() < 1 || instructions.size() > 50) {
        std::cout << "Invalid command: instruction count must be between 1 and 50\n";
        return;
    }

    auto screen = std::make_shared<Screen>(name, instructions, globalProcessId++);
    screen->setAllocatedMemory(memorySize);

    // Allocate memory through memory manager
    if (memoryManager && memoryManager->allocateProcess(screen->getProcessId(), memorySize)) {
        registerProcess(screen);
        std::cout << "Process '" << name << "' created with " << memorySize << " bytes and custom instructions.\n";
    }
    else {
        std::cout << "Failed to allocate memory for process '" << name << "'.\n";
    }
}

// CHANGE: Helper method to parse instruction string
std::vector<Instruction> ProcessManager::parseInstructionString(const std::string& instructionString) {
    std::vector<Instruction> instructions;
    std::stringstream ss(instructionString);
    std::string token;

    while (std::getline(ss, token, ';')) {
        // Trim whitespace

        token.erase(0, token.find_first_not_of(" \t"));
        token.erase(token.find_last_not_of(" \t") + 1);

        if (token.empty()) continue;

        std::stringstream instrStream(token);
        std::string command;
        instrStream >> command;

        Instruction instr;

        if (command == "DECLARE") {
            instr.type = InstructionType::DECLARE;
            std::string var, value;
            instrStream >> var >> value;
            instr.args = { var, value };
        }
        else if (command == "ADD") {
            instr.type = InstructionType::ADD;
            std::string dest, op1, op2;
            instrStream >> dest >> op1 >> op2;
            instr.args = { dest, op1, op2 };
        }
        else if (command == "SUBTRACT") {
            instr.type = InstructionType::SUBTRACT;
            std::string dest, op1, op2;
            instrStream >> dest >> op1 >> op2;
            instr.args = { dest, op1, op2 };
        }
        else if (command == "READ") {
            instr.type = InstructionType::READ;
            std::string var, addr;
            instrStream >> var >> addr;
            instr.args = { var, addr };
        }
        else if (command == "WRITE") {
            instr.type = InstructionType::WRITE;
            std::string addr, value;
            instrStream >> addr >> value;
            instr.args = { addr, value };
        }
        else if (command.rfind("PRINT", 0) == 0) { // command starts with PRINT
            // If "PRINT" was stuck together with the rest, strip it
            if (command != "PRINT") {
                // Put back the extra chars into remaining
                std::string afterPrint = command.substr(5); // remove "PRINT"
                token = afterPrint + " " + token.substr(token.find(command) + command.size());
            }
            else {
                std::getline(instrStream, token);
            }

            // Trim whitespace
            token.erase(0, token.find_first_not_of(" \t"));
            token.erase(token.find_last_not_of(" \t") + 1);

            // Replace escaped quotes
            size_t pos;
            while ((pos = token.find("\\\"")) != std::string::npos) {
                token.replace(pos, 2, "\"");
            }

            // Remove parentheses
            if (!token.empty() && token.front() == '(' && token.back() == ')') {
                token = token.substr(1, token.size() - 2);
            }

            instr.type = InstructionType::PRINT;
            instr.args = { token.empty() ? "Hello World" : token };
        }

        else if (command == "SLEEP") {
            instr.type = InstructionType::SLEEP;
            std::string duration;
            instrStream >> duration;
            instr.args = { duration };
        }

        instructions.push_back(instr);
    }

    return instructions;
}

void ProcessManager::swapOut(const std::string& name) {
    std::lock_guard<std::mutex> lock(processMutex);
    if (processes.count(name)) {
        auto process = processes[name];
        if (memoryManager) {
            memoryManager->pageOut(process->getProcessId()); // Assuming a pageOut function that takes a process ID
        }
        std::cout << "[MEMORY] Swapped out process: " << name << std::endl;
    }
}

void ProcessManager::swapIn(const std::string& name) {
    std::lock_guard<std::mutex> lock(processMutex);
    if (processes.count(name)) {
        auto process = processes[name];
        if (memoryManager) {
            // Need to specify a page to swap in, assuming it pages in the first page as an example.
            memoryManager->pageIn(process->getProcessId(), 0);
        }
        std::cout << "[MEMORY] Swapped in process: " << name << std::endl;
    }
}

void ProcessManager::registerProcess(const std::shared_ptr<Screen>& process) {
    std::lock_guard<std::mutex> lock(processMutex);
    processes[process->getName()] = process;
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

    // Ready Processes
    std::cout << "\nReady Processes:\n";
    int cntReady = 0;
    for (const auto& pair : processes) {
        const std::string& name = pair.first;
        const std::shared_ptr<Screen>& proc = pair.second;

        if (proc->getStatus() == ProcessStatus::READY) {
            cntReady++;
            std::cout << std::setw(15) << std::left << ("- " + name)
                << std::setw(22) << ("(" + proc->getCreationTimestamp() + ")")
                << "Awaiting CPU\n";
        }
    }
    if (cntReady == 0) std::cout << "No ready processes.\n";

    // Running Processes
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

    // Finished Processes
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

void ProcessManager::generateReport() {
    std::lock_guard<std::mutex> lock(processMutex);
    // Implementation for generating a report
    std::cout << "[REPORT] Generating report for all processes...\n";
    // For each process, you would print its status, resource usage, etc.
    if (memoryManager) {
        memoryManager->printReport();
    }
    // You can also add more details about each process here.
    std::cout << "[REPORT] Report generation complete.\n";
}

std::shared_ptr<Screen> ProcessManager::getProcess(const std::string& name) {
    std::lock_guard<std::mutex> lock(processMutex);
    if (processes.count(name)) {
        return processes[name];
    }
    return nullptr;
}

bool ProcessManager::hasProcess(const std::string& name) {
    std::lock_guard<std::mutex> lock(processMutex);
    return processes.count(name) > 0;
}

void ProcessManager::cleanupFinishedProcesses() {
    std::lock_guard<std::mutex> lock(processMutex);

    std::vector<std::string> toRemove;

    for (auto& pair : processes) {
        auto& screen = pair.second;

        // Ensure process is truly finished before cleanup
        if (screen->isFinished() && screen->getStatus() == ProcessStatus::FINISHED) {
            int pid = screen->getProcessId();

            // Clear core assignment before cleanup
            screen->setCoreAssigned(-1);

            if (memoryManager) {
                memoryManager->deallocateProcess(pid);
            }

            //toRemove.push_back(pair.first);
        }
    }

    // Remove processes after iteration
    //for (const auto& name : toRemove) {
      //  processes.erase(name);
    //}




}

double ProcessManager::getCpuUtilization(int numCores) {
    std::lock_guard<std::mutex> lock(processMutex);

    std::unordered_set<int> activeCoreIds;
    for (const auto& pair : processes) {
        const std::shared_ptr<Screen>& proc = pair.second;
        if (!proc->isFinished() && proc->getCoreAssigned() != -1) {
            activeCoreIds.insert(proc->getCoreAssigned());
        }
    }

    int activeCores = static_cast<int>(activeCoreIds.size());
    if (numCores == 0) {
        return 0.0;
    }

    return (static_cast<double>(activeCores) / numCores) * 100.0;
}

