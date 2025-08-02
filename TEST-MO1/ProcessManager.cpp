#include "ProcessManager.h"
#include "Scheduler.h"
#include "MemoryManager.h"  // CHANGE: Include for memory operations

#include <iostream>
#include <fstream>
#include <iomanip>
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
                                   // CHANGE: Add READ/WRITE instruction generation
        case InstructionType::READ: {
            std::string var = variables[rand() % variables.size()];
            uint32_t addr = 0x1000 + (rand() % 16) * 0x100;
            std::ostringstream oss;
            oss << "0x" << std::hex << addr;
            instr.args = { var, oss.str() };
            break;
        }
        case InstructionType::WRITE: {
            uint32_t addr = 0x1000 + (rand() % 16) * 0x100;
            std::ostringstream oss;
            oss << "0x" << std::hex << addr;
            int value = rand() % 100 + 1;
            instr.args = { oss.str(), std::to_string(value) };
            break;
        }
        default:
            break;
        }

        return instr;
        };

    for (int i = 0; i < numInstructions; ++i) {
        // CHANGE: Include READ/WRITE in random generation (0-6 range)
        int choice = rand() % 7;  // 0 to 6 to include READ/WRITE
        InstructionType type = static_cast<InstructionType>(choice);
        instructions.push_back(generateSimpleInstruction(type));
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
        else if (command == "PRINT") {
            instr.type = InstructionType::PRINT;
            std::string remaining;
            std::getline(instrStream, remaining);
            // Remove quotes if present
            if (!remaining.empty() && remaining[0] == '(') {
                size_t start = remaining.find('"');
                size_t end = remaining.rfind('"');
                if (start != std::string::npos && end != std::string::npos && start < end) {
                    remaining = remaining.substr(start + 1, end - start - 1);
                }
            }
            instr.args = { remaining };
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
    std::cout << "-------------------------------------------\n";
    std::cout << "| Currently Loaded Processes              |\n";
    std::cout << "-------------------------------------------\n";
    std::cout << std::left << std::setw(15) << "Name"
        << std::left << std::setw(10) << "PID"
        << std::left << std::setw(15) << "Status"
        << std::left << "Core\n";
    std::cout << "-------------------------------------------\n";

    for (const auto& pair : processes) {
        const auto& process = pair.second;
        std::cout << std::left << std::setw(15) << process->getName()
            << std::left << std::setw(10) << process->getProcessId()
            << std::left << std::setw(15) << process->getStatusString()
            << std::left << ((process->getCoreAssigned() == -1) ? "N/A" : std::to_string(process->getCoreAssigned())) << "\n";
    }
    std::cout << "-------------------------------------------\n";
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