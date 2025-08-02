#include "Screen.h"
#include "MemoryManager.h"  // CHANGE: Include for memory operations
#include <iostream>
#include <chrono>
#include <ctime>
#include <iomanip>
#include <fstream>
#include <sstream>
#include <thread>
#include <limits>
#include "CLIUtils.h"
#include <unordered_map>

// CHANGE: Include external memory manager
extern std::unique_ptr<MemoryManager> memoryManager;

bool scheduled = false;

// Constructor
Screen::Screen()
    : name("default"), instructionPointer(0),
    status(ProcessStatus::READY), coreAssigned(-1), errorFlag(false), processId(0),
    allocatedMemorySize(0)  // CHANGE: Initialize new member
{
    updateTimestamp();
    instructions.clear();
}

Screen::Screen(const std::string& name_, const std::vector<Instruction>& instrs, int id)
    : name(name_), instructions(instrs), instructionPointer(0),
    status(ProcessStatus::READY), coreAssigned(-1), errorFlag(false), processId(id),
    allocatedMemorySize(0)  // CHANGE: Initialize new member
{
    updateTimestamp();
    logFile.open(name + ".log", std::ios::app);
    if (!logFile.is_open()) {
        std::cerr << "Failed to open log file for process: " << name << std::endl;
    }
}

bool Screen::isNumber(const std::string& s) const {
    if (s.empty()) return false;
    for (char c : s)
        if (!isdigit(c) && c != '-') return false;
    return true;
}

int Screen::resolveValue(const std::string& token) {
    // CHANGE: Use memory manager for variable resolution
    if (isNumber(token)) return std::stoi(token);

    if (memoryManager) {
        return memoryManager->getVariable(processId, token);
    }

    // Fallback to original implementation
    if (!memory.count(token)) memory[token] = 0;
    return memory[token];
}

void Screen::executeNextInstruction() {
    assignCoreIfUnassigned(4);

    if (instructions.empty()) {
        printLog("No instructions loaded yet. Wait for scheduler.");
        std::cout << "[INFO] Process not yet scheduled. Please run 'scheduler-start'.\n";
        return;
    }

    if (status == ProcessStatus::FINISHED || instructionPointer >= instructions.size()) {
        status = ProcessStatus::FINISHED;
        printLog("Process already finished.");
        return;
    }

    const Instruction& instr = instructions[instructionPointer];

    try {
        // CHANGE: Enhanced instruction execution with memory operations
        if (instr.type == InstructionType::PRINT && !instr.args.empty()) {
            auto now = std::chrono::system_clock::now();
            std::time_t tnow = std::chrono::system_clock::to_time_t(now);
            std::tm localTime{};
#ifdef _WIN32
            localtime_s(&localTime, &tnow);
#else
            localtime_r(&tnow, &localTime);
#endif
            char timeBuf[40];
            std::strftime(timeBuf, sizeof(timeBuf), "(%m/%d/%Y %I:%M:%S%p)", &localTime);

            std::stringstream ss;
            ss << timeBuf << " Core:" << getCoreAssigned() << " \"" << instr.args[0] << "\"";
            std::string logEntry = ss.str();

            std::ofstream logFile(name + ".log", std::ios::app);
            if (logFile.is_open()) {
                logFile << logEntry << "\n";
            }
        }
        else if (instr.type == InstructionType::SLEEP && !instr.args.empty()) {
            try {
                int duration = std::stoi(instr.args[0]);
                std::this_thread::sleep_for(std::chrono::seconds(duration));
            }
            catch (...) {
                std::cerr << "[ERROR] Invalid sleep duration: " << instr.args[0] << "\n";
                errorFlag = true;
            }
        }
        else if (instr.type == InstructionType::DECLARE && instr.args.size() == 2) {
            const std::string& varName = instr.args[0];
            try {
                uint16_t value = static_cast<uint16_t>(std::stoi(instr.args[1]));

                // CHANGE: Use memory manager for variable declaration
                if (memoryManager) {
                    if (!memoryManager->declareVariable(processId, varName, value)) {
                        std::cerr << "[ERROR] Failed to declare variable " << varName << " (symbol table full)\n";
                        errorFlag = true;
                        return;
                    }
                }
                else {
                    // Fallback to original implementation
                    memory[varName] = value;
                }

                printLog("DECLARE " + varName + " = " + std::to_string(value));
            }
            catch (...) {
                std::cerr << "[ERROR] Invalid DECLARE value: " << instr.args[1] << std::endl;
                errorFlag = true;
            }
        }
        else if (instr.type == InstructionType::ADD && instr.args.size() == 3) {
            const std::string& var1 = instr.args[0];
            try {
                int op1 = resolveValue(instr.args[1]);
                int op2 = resolveValue(instr.args[2]);
                uint16_t result = static_cast<uint16_t>(op1 + op2);

                // CHANGE: Use memory manager for variable storage
                if (memoryManager) {
                    memoryManager->setVariable(processId, var1, result);
                }
                else {
                    // Fallback to original implementation
                    if (!memory.count(var1)) memory[var1] = 0;
                    memory[var1] = result;
                }

                printLog("ADD " + var1 + " = " + std::to_string(op1) + " + " + std::to_string(op2) + " = " + std::to_string(result));
            }
            catch (const std::exception& e) {
                std::cerr << "[ERROR] Invalid ADD operands: " << e.what() << "\n";
                errorFlag = true;
            }
        }
        else if (instr.type == InstructionType::SUBTRACT && instr.args.size() == 3) {
            const std::string& var1 = instr.args[0];
            try {
                int op1 = resolveValue(instr.args[1]);
                int op2 = resolveValue(instr.args[2]);
                uint16_t result = static_cast<uint16_t>(op1 - op2);

                // CHANGE: Use memory manager for variable storage
                if (memoryManager) {
                    memoryManager->setVariable(processId, var1, result);
                }
                else {
                    // Fallback to original implementation
                    if (!memory.count(var1)) memory[var1] = 0;
                    memory[var1] = result;
                }

                printLog("SUBTRACT " + var1 + " = " + std::to_string(op1) + " - " + std::to_string(op2) + " = " + std::to_string(result));
            }
            catch (const std::exception& e) {
                std::cerr << "[ERROR] Invalid SUBTRACT operands: " << e.what() << "\n";
                errorFlag = true;
            }
        }
        // CHANGE: New READ instruction implementation
        else if (instr.type == InstructionType::READ && instr.args.size() == 2) {
            const std::string& varName = instr.args[0];
            const std::string& addressStr = instr.args[1];

            try {
                // Parse hexadecimal address
                uint32_t address;
                if (addressStr.substr(0, 2) == "0x" || addressStr.substr(0, 2) == "0X") {
                    address = std::stoul(addressStr, nullptr, 16);
                }
                else {
                    address = std::stoul(addressStr, nullptr, 10);
                }

                if (memoryManager) {
                    uint16_t value = memoryManager->readMemory(processId, address);
                    memoryManager->setVariable(processId, varName, value);
                    printLog("READ " + varName + " from " + addressStr + " = " + std::to_string(value));
                }
                else {
                    std::cerr << "[ERROR] Memory manager not initialized\n";
                    errorFlag = true;
                }
            }
            catch (const std::exception& e) {
                std::cerr << "[ERROR] Memory access violation: " << e.what() << "\n";

                // Record error details for later reporting
                auto now = std::time(nullptr);
                std::tm tm_now{};
                char buf[100];
#ifdef _WIN32
                localtime_s(&tm_now, &now);
#else
                localtime_r(&now, &tm_now);
#endif
                std::strftime(buf, sizeof(buf), "%H:%M:%S", &tm_now);

                setErrorInfo(e.what(), buf);
                status = ProcessStatus::FINISHED;
                return;
            }
        }
        // CHANGE: New WRITE instruction implementation
        else if (instr.type == InstructionType::WRITE && instr.args.size() == 2) {
            const std::string& addressStr = instr.args[0];
            const std::string& valueStr = instr.args[1];

            try {
                // Parse hexadecimal address
                uint32_t address;
                if (addressStr.substr(0, 2) == "0x" || addressStr.substr(0, 2) == "0X") {
                    address = std::stoul(addressStr, nullptr, 16);
                }
                else {
                    address = std::stoul(addressStr, nullptr, 10);
                }

                // Parse value (could be variable name or literal)
                uint16_t value;
                if (isNumber(valueStr)) {
                    value = static_cast<uint16_t>(std::stoi(valueStr));
                }
                else {
                    // It's a variable name
                    if (memoryManager) {
                        value = memoryManager->getVariable(processId, valueStr);
                    }
                    else {
                        value = memory.count(valueStr) ? memory[valueStr] : 0;
                    }
                }

                if (memoryManager) {
                    memoryManager->writeMemory(processId, address, value);
                    printLog("WRITE " + std::to_string(value) + " to " + addressStr);
                }
                else {
                    std::cerr << "[ERROR] Memory manager not initialized\n";
                    errorFlag = true;
                }
            }
            catch (const std::exception& e) {
                std::cerr << "[ERROR] Memory access violation: " << e.what() << "\n";

                // Record error details for later reporting
                auto now = std::time(nullptr);
                std::tm tm_now{};
                char buf[100];
#ifdef _WIN32
                localtime_s(&tm_now, &now);
#else
                localtime_r(&now, &tm_now);
#endif
                std::strftime(buf, sizeof(buf), "%H:%M:%S", &tm_now);

                setErrorInfo(e.what(), buf);
                status = ProcessStatus::FINISHED;
                return;
            }
        }
    }
    catch (const std::exception& e) {
        std::cerr << "[ERROR] Exception during instruction execution: " << e.what() << "\n";
        errorFlag = true;
        status = ProcessStatus::FINISHED;
        return;
    }

    instructionPointer++;
    if (instructionPointer >= instructions.size()) {
        status = ProcessStatus::FINISHED;
        printLog("Process finished execution.");
    }
}

void Screen::assignCoreIfUnassigned(int totalCores) {
    if (coreAssigned == -1) {
        coreAssigned = rand() % totalCores;
    }
}

void Screen::advanceInstruction() {
    if (instructionPointer < instructions.size()) {
        ++instructionPointer;
    }
}

void Screen::showScreen() {
    while (true) {
#ifdef _WIN32
        system("cls");
#else
        system("clear");
#endif

        std::cout << "root:\\> (process-smi / exit): ";
        std::string input;
        std::getline(std::cin, input);

        if (input == "exit") {
            break;
        }
        else if (input == "process-smi") {
            executeNextInstruction();

            std::cout << "\nProcess Name:   " << getName() << "\n";
            std::cout << "Process ID:     " << getProcessId() << "\n";

            // CHANGE: Display memory information
            if (allocatedMemorySize > 0) {
                std::cout << "Memory Size:    " << allocatedMemorySize << " bytes\n";
            }

            std::cout << "Logs:\n";
            std::ifstream readLog(name + ".log");
            bool hasLogs = false;
            if (readLog.is_open()) {
                std::string line;
                while (std::getline(readLog, line)) {
                    if (!line.empty()) {
                        hasLogs = true;
                        std::cout << line << "\n";
                    }
                }
                readLog.close();
            }

            if (!hasLogs) {
                std::cout << "[No logs available for this process]\n";
            }

            if (!isScheduled()) {
                std::cout << "\nCurrent Instruction Line: 0\n";
                std::cout << "Lines of Code:            0\n";
            }
            else if (getStatus() != ProcessStatus::FINISHED) {
                std::cout << "\nCurrent Instruction Line: " << getCurrentInstruction() << "\n";
                std::cout << "Lines of Code:            " << getTotalInstructions() << "\n";
            }

            switch (getStatus()) {
            case ProcessStatus::READY: std::cout << "\nReady!"; break;
            case ProcessStatus::RUNNING: std::cout << "\nRunning!"; break;
            case ProcessStatus::FINISHED:
                std::cout << "\nFinished!";
                // CHANGE: Display memory violation info if applicable
                if (hasError() && !errorMessage.empty()) {
                    std::cout << "\n** ERROR: " << errorMessage << " **";
                }
                break;
            }

            std::cout << "\n\nPress ENTER to continue...";
            std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
        }
        else {
            std::cout << "Unknown command. Use 'process-smi' or 'exit'.\n\n";
        }
    }

    CLIUtils::clearScreen();
    CLIUtils::printHeader();
}

void Screen::generateDummyInstructions(const Config& config) {
    std::lock_guard<std::mutex> lock(mtx);

    std::vector<std::string> variables = { "x", "y", "z", "a", "b", "c" };
    std::vector<Instruction> instrs;
    int count = rand() % (config.maxIns - config.minIns + 1) + config.minIns;

    auto generateSimpleInstruction = [&](InstructionType type) -> Instruction {
        Instruction instr;
        instr.type = type;

        switch (type) {
        case InstructionType::DECLARE: {
            std::string var = variables[rand() % variables.size()];
            int value = rand() % 20 + 1;
            instr.args = { var, std::to_string(value) };
            break;
        }
        case InstructionType::ADD:
        case InstructionType::SUBTRACT: {
            std::string dest = variables[rand() % variables.size()];
            std::string op1 = variables[rand() % variables.size()];
            std::string op2 = variables[rand() % variables.size()];
            instr.args = { dest, op1, op2 };
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
                                   // CHANGE: Add generation for READ/WRITE instructions
        case InstructionType::READ: {
            std::string var = variables[rand() % variables.size()];
            uint32_t addr = 0x1000 + (rand() % 16) * 0x100; // Random addresses
            std::ostringstream oss;
            oss << "0x" << std::hex << addr;
            instr.args = { var, oss.str() };
            break;
        }
        case InstructionType::WRITE: {
            uint32_t addr = 0x1000 + (rand() % 16) * 0x100; // Random addresses
            std::ostringstream oss;
            oss << "0x" << std::hex << addr;
            int value = rand() % 100 + 1;
            instr.args = { oss.str(), std::to_string(value) };
            break;
        }
        default: break;
        }

        return instr;
        };

    for (int i = 0; i < count; ++i) {
        // CHANGE: Include READ/WRITE in instruction generation (0-6 range now)
        int choice = rand() % 7;  // 0–6 to include READ/WRITE
        InstructionType type = static_cast<InstructionType>(choice);
        instrs.push_back(generateSimpleInstruction(type));
    }

    instructions = instrs;
    instructionPointer = 0;
    status = ProcessStatus::READY;
}

// Rest of the methods remain the same...
void Screen::printLog(const std::string& msg) {
    std::lock_guard<std::mutex> lock(mtx);
    if (logFile.is_open()) {
        logFile << "(" << creationTimestamp << ") " << msg << std::endl;
        logFile.flush();
    }
}

void Screen::updateTimestamp() {
    auto now = std::chrono::system_clock::now();
    std::time_t tnow = std::chrono::system_clock::to_time_t(now);
    std::tm localTime{};
#ifdef _WIN32
    localtime_s(&localTime, &tnow);
#else
    localtime_r(&tnow, &localTime);
#endif
    char buffer[20];
    std::strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", &localTime);
    creationTimestamp = buffer;
}

// Getters & setters (unchanged)
std::string Screen::getName() const {
    std::lock_guard<std::mutex> lock(mtx);
    return name;
}

void Screen::setName(const std::string& newName) {
    std::lock_guard<std::mutex> lock(mtx);
    name = newName;
}

std::string Screen::getCreationTimestamp() const {
    std::lock_guard<std::mutex> lock(mtx);
    return creationTimestamp;
}

size_t Screen::getCurrentInstruction() const {
    std::lock_guard<std::mutex> lock(mtx);
    return instructionPointer + 1;
}

void Screen::setInstructions(const std::vector<Instruction>& instrs) {
    std::lock_guard<std::mutex> lock(mtx);
    instructions = instrs;
    instructionPointer = 0;
    scheduled = true;
    status = ProcessStatus::READY;
}

void Screen::setScheduled(bool value) {
    std::lock_guard<std::mutex> lock(mtx);
    scheduled = value;
}

bool Screen::isScheduled() const {
    std::lock_guard<std::mutex> lock(mtx);
    return scheduled;
}

size_t Screen::getTotalInstructions() const {
    std::lock_guard<std::mutex> lock(mtx);
    return instructions.size();
}

void Screen::setCoreAssigned(int core) {
    std::lock_guard<std::mutex> lock(mtx);
    coreAssigned = core;
}

int Screen::getCoreAssigned() const {
    std::lock_guard<std::mutex> lock(mtx);
    return coreAssigned;
}

void Screen::setStatus(ProcessStatus newStatus) {
    std::lock_guard<std::mutex> lock(mtx);
    status = newStatus;
}

ProcessStatus Screen::getStatus() const {
    std::lock_guard<std::mutex> lock(mtx);
    return status;
}

bool Screen::isFinished() const {
    std::lock_guard<std::mutex> lock(mtx);
    return status == ProcessStatus::FINISHED;
}

void Screen::setError(bool err) {
    errorFlag = err;
}

bool Screen::hasError() const {
    return errorFlag;
}

void Screen::truncateInstructions(int n) {
    std::lock_guard<std::mutex> lock(mtx);
    if (n < instructions.size()) {
        instructions.resize(n);
    }
}

std::string Screen::getTimestamp() const {
    auto now = std::chrono::system_clock::now();
    std::time_t tnow = std::chrono::system_clock::to_time_t(now);
    std::tm localTime{};
#ifdef _WIN32
    localtime_s(&localTime, &tnow);
#else
    localtime_r(&tnow, &localTime);
#endif
    char buffer[20];
    std::strftime(buffer, sizeof(buffer), "%Y/%m/%d %H:%M:%S", &localTime);
    return std::string(buffer);
}

int Screen::getProcessId() const {
    return processId;
}

std::string Screen::getStatusString() const {
    switch (status) {
    case ProcessStatus::READY: return "READY";
    case ProcessStatus::RUNNING: return "RUNNING";
    case ProcessStatus::FINISHED: return "FINISHED";
    default: return "UNKNOWN";
    }
}
