#include "Screen.h"
#include "MemoryManager.h"  // CHANGE: Include for memory operations
#include "Scheduler.h"  
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
    allocatedMemorySize(0), isSleeping(false), sleepRemainingTicks(0), sleepStartTick(0) // CHANGE: Initialize new member
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

    if (hasMemoryError) {
        return; // Don't execute if there's an error
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

            // Enhanced PRINT processing to handle variable concatenation
            std::string printExpression = instr.args[0];
            std::string finalOutput;

            // Process the expression to handle string concatenation with variables
            // Example: "Variable A: " + varA becomes "Variable A: 15"

            size_t pos = 0;
            while (pos < printExpression.length()) {
                // Look for quoted strings
                if (printExpression[pos] == '"') {
                    size_t endQuote = printExpression.find('"', pos + 1);
                    if (endQuote != std::string::npos) {
                        // Extract the quoted string (without quotes)
                        std::string quotedText = printExpression.substr(pos + 1, endQuote - pos - 1);
                        finalOutput += quotedText;
                        pos = endQuote + 1;

                        // Skip whitespace and potential '+' operator
                        while (pos < printExpression.length() &&
                            (printExpression[pos] == ' ' || printExpression[pos] == '\t' || printExpression[pos] == '+')) {
                            pos++;
                        }
                    }
                    else {
                        pos++;
                    }
                }
                // Look for variable names (not in quotes)
                else if (std::isalpha(printExpression[pos]) || printExpression[pos] == '_') {
                    size_t varStart = pos;
                    while (pos < printExpression.length() &&
                        (std::isalnum(printExpression[pos]) || printExpression[pos] == '_')) {
                        pos++;
                    }

                    std::string varName = printExpression.substr(varStart, pos - varStart);

                    // Get variable value
                    uint16_t varValue = 0;
                    if (memoryManager) {
                        varValue = memoryManager->getVariable(processId, varName);
                    }
                    else {
                        // Fallback to local memory
                        if (memory.count(varName)) {
                            varValue = memory[varName];
                        }
                    }

                    finalOutput += std::to_string(varValue);

                    // Skip whitespace and potential '+' operator
                    while (pos < printExpression.length() &&
                        (printExpression[pos] == ' ' || printExpression[pos] == '\t' || printExpression[pos] == '+')) {
                        pos++;
                    }
                }
                else {
                    pos++;
                }
            }

            // If no processing occurred, use the original expression
            if (finalOutput.empty()) {
                finalOutput = printExpression;

                // Remove quotes if it's a simple quoted string
                if (finalOutput.length() >= 2 && finalOutput.front() == '"' && finalOutput.back() == '"') {
                    finalOutput = finalOutput.substr(1, finalOutput.length() - 2);
                }
            }

            std::stringstream ss;
            ss << timeBuf << " Core:" << getCoreAssigned() << " \"" << finalOutput << "\"";
            std::string logEntry = ss.str();

            
            std::ofstream logFile(name + ".log", std::ios::app);
            if (logFile.is_open()) {
                logFile << logEntry << "\n";
                logFile.flush();
            }

            // Also print to console for immediate feedback
           // std::cout << "[PRINT] " << name << ": " << finalOutput << std::endl;
        }

        else if (instr.type == InstructionType::SLEEP && !instr.args.empty()) {
            int sleepDuration = std::stoi(instr.args[0]);
            // Convert sleep duration to CPU ticks
            // If sleep duration is in seconds, multiply by some tick rate
            // Or treat the value directly as CPU ticks
            int sleepTicks = sleepDuration; // Treat as direct CPU tick count

            // Set the process to sleeping state
            isSleeping = true;
            sleepRemainingTicks = sleepTicks;
            sleepStartTick = memoryManager->getCurrentTick(); // Current global CPU tick

            printLog("SLEEP " + std::to_string(sleepDuration) + " ticks - process going to sleep");
            //std::cout << "[SLEEP] Process " << name << " going to sleep for " << sleepTicks << " ticks\n";

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
                if (instr.args.size() >= 3) {
                    std::string dest = instr.args[0];
                    std::string op1 = instr.args[1];
                    std::string op2 = instr.args[2];

                    uint16_t val1, val2;

                    // Get values (may cause page faults)
                    if (memoryManager) {
                        val1 = memoryManager->getVariable(processId, op1);
                        val2 = memoryManager->getVariable(processId, op2);

                        uint16_t result = val1 + val2;
                        if (!memoryManager->declareVariable(processId, dest, result)) {
                            setError(true);
                            errorMessage = "Failed to store ADD result: page fault or memory violation";
                            return;
                        }
                    }
                    else {
                        val1 = variables[op1];
                        val2 = variables[op2];
                        variables[dest] = val1 + val2;
                    }

                    printLog("ADD " + dest + " = " + std::to_string(val1) + " + " + std::to_string(val2) + " = " + std::to_string(val1 + val2));
                }
            }
            catch (const std::exception& e) {
                std::cerr << "[ERROR] Invalid ADD operands: " << e.what() << "\n";
                errorFlag = true;
            }
        }
        else if (instr.type == InstructionType::SUBTRACT && instr.args.size() == 3) {
            const std::string& var1 = instr.args[0];
            try {
                if (instr.args.size() >= 3) {
                    std::string dest = instr.args[0];
                    std::string op1 = instr.args[1];
                    std::string op2 = instr.args[2];

                    uint16_t val1, val2;

                    if (memoryManager) {
                        val1 = memoryManager->getVariable(processId, op1);
                        val2 = memoryManager->getVariable(processId, op2);

                        uint16_t result = val1 - val2;
                        if (!memoryManager->declareVariable(processId, dest, result)) {
                            setError(true);
                            errorMessage = "Failed to store SUBTRACT result: page fault or memory violation";
                            return;
                        }
                    }
                    else {
                        val1 = variables[op1];
                        val2 = variables[op2];
                        variables[dest] = val1 - val2;
                    }

                    printLog("SUBTRACT " + dest + " = " + std::to_string(val1) + " - " + std::to_string(val2) + " = " + std::to_string(val1 - val2));
                }
            }
            catch (const std::exception& e) {
                std::cerr << "[ERROR] Invalid SUBTRACT operands: " << e.what() << "\n";
                errorFlag = true;
            }
        }
        // CHANGE: New READ instruction implementation
        else if (instr.type == InstructionType::READ && instr.args.size() == 2) {
          
            try {
                if (instr.args.size() >= 2) {
                    std::string varName = instr.args[0];
                    std::string addrStr = instr.args[1];

                    // Parse address (hex format)
                    uint32_t address;
                    if (addrStr.substr(0, 2) == "0x" || addrStr.substr(0, 2) == "0X") {
                        address = static_cast<uint32_t>(std::stoul(addrStr, nullptr, 16));
                    }
                    else {
                        address = static_cast<uint32_t>(std::stoul(addrStr));
                    }

                    if (memoryManager) {
                        try {
                            // This may cause page fault - memory manager should handle it
                            uint16_t value = memoryManager->readMemory(processId, address);

                            // Store the read value in variable (may also cause page fault)
                            if (!memoryManager->declareVariable(processId, varName, value)) {
                                setError(true);
                                errorMessage = "Failed to store READ result: symbol table page fault";
                                return;
                            }

                            printLog("READ " + varName + " from address " + addrStr + " = " + std::to_string(value));
                        }
                        catch (const std::exception& e) {
                            // Check if it's a memory violation or a page fault
                            if (memoryManager->hasProcessViolation(processId)) {
                                setError(true);
                                errorMessage = "Memory access violation at address " + addrStr;
                                return;
                            }
                            else {
                                // This should be a page fault - retry the instruction
                                // Don't increment currentInstruction
                                printLog("Page fault during READ at address " + addrStr + " - retrying");
                                return;
                            }
                        }
                    }
                    else {
                        variables[varName] = 0; // Fallback
                        printLog("READ " + varName + " from address " + addrStr + " = 0 (no memory manager)");
                    }
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
                if (instr.args.size() >= 2) {
                    std::string addrStr = instr.args[0];
                    std::string valueStr = instr.args[1];

                    // Parse address
                    uint32_t address;
                    if (addrStr.substr(0, 2) == "0x" || addrStr.substr(0, 2) == "0X") {
                        address = static_cast<uint32_t>(std::stoul(addrStr, nullptr, 16));
                    }
                    else {
                        address = static_cast<uint32_t>(std::stoul(addrStr));
                    }

                    // Parse value (could be variable name or literal)
                    uint16_t value;
                    if (std::isdigit(valueStr[0]) || valueStr[0] == '-') {
                        value = static_cast<uint16_t>(std::stoi(valueStr));
                    }
                    else {
                        // It's a variable name
                        if (memoryManager) {
                            value = memoryManager->getVariable(processId, valueStr);
                        }
                        else {
                            value = variables[valueStr];
                        }
                    }

                    if (memoryManager) {
                        try {
                            // This may cause page fault - memory manager should handle it
                            memoryManager->writeMemory(processId, address, value);
                            printLog("WRITE " + std::to_string(value) + " to address " + addrStr);
                        }
                        catch (const std::exception& e) {
                            // Check if it's a memory violation or a page fault
                            if (memoryManager->hasProcessViolation(processId)) {
                                setError(true);
                                errorMessage = "Memory access violation at address " + addrStr;
                                return;
                            }
                            else {
                                // This should be a page fault - retry the instruction
                                printLog("Page fault during WRITE at address " + addrStr + " - retrying");
                                return;
                            }
                        }
                    }
                    else {
                        printLog("WRITE " + std::to_string(value) + " to address " + addrStr + " (no memory manager)");
                    }
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

bool Screen::checkSleepComplete(){
    if (!isSleeping) return true;

    if (sleepRemainingTicks <= 0) {
        isSleeping = false;
        sleepRemainingTicks = 0;

        // Now advance the instruction pointer since sleep is complete
        instructionPointer++;

        printLog("SLEEP completed - process waking up");
        //std::cout << "[SLEEP] Process " << name << " waking up from sleep\n";
        return true;
    }

    return false;
}
