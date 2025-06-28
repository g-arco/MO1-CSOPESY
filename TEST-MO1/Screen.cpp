#include "Screen.h"
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
#include "Scheduler.h"

bool scheduled = false;

// Constructor
Screen::Screen()
    : name("default"), instructionPointer(0),
      status(ProcessStatus::READY), coreAssigned(-1), errorFlag(false), processId(0)
{
    updateTimestamp();
    instructions.clear();
}

Screen::Screen(const std::string& name_, const std::vector<Instruction>& instrs, int id)
    : name(name_), instructions(instrs), instructionPointer(0),
      status(ProcessStatus::READY), coreAssigned(-1), errorFlag(false), processId(id)
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
    if (isNumber(token)) return std::stoi(token);
    if (memory.count(token)) return memory[token];
    throw std::runtime_error("Unknown variable: " + token);
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

    // Helper: clamp to uint16 range
    auto clamp_uint16 = [](int value) -> int {
        if (value < 0) return 0;
        if (value > 65535) return 65535;
        return value;
        };

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

        std::cout << logEntry << std::endl;
    }
    else if (instr.type == InstructionType::SLEEP && !instr.args.empty()) {
        try {
            int ticks = std::stoi(instr.args[0]);
            std::cout << "[INFO] Sleeping for " << ticks << " CPU tick(s)..." << std::endl;

            status = ProcessStatus::SLEEPING;
            // You should set sleepUntilTick = cpuTicks + ticks in scheduler context
            return;  // relinquish CPU; scheduler requeues later
        }
        catch (...) {
            std::cerr << "[ERROR] Invalid sleep tick count: " << instr.args[0] << "\n";
            errorFlag = true;
        }
    }
    else if (instr.type == InstructionType::DECLARE && instr.args.size() == 2) {
        const std::string& varName = instr.args[0];
        try {
            int value = std::stoi(instr.args[1]);
            value = clamp_uint16(value);
            memory[varName] = value;

            std::cout << "[INFO] DECLARE: " << varName << " = " << value << std::endl;
            printLog("DECLARE " + varName + " = " + std::to_string(value));
        }
        catch (...) {
            std::cerr << "[ERROR] Invalid DECLARE value: " << instr.args[1] << std::endl;
            errorFlag = true;
        }
    }
    else if (instr.type == InstructionType::ADD && instr.args.size() == 3) {
        const std::string& dest = instr.args[0];
        const std::string& op1 = instr.args[1];
        const std::string& op2 = instr.args[2];

        try {
            int val1 = (memory.count(op1) ? memory[op1] : std::stoi(op1));
            int val2 = (memory.count(op2) ? memory[op2] : std::stoi(op2));
            int result = clamp_uint16(val1 + val2);
            memory[dest] = result;

            std::cout << "[INFO] ADD: " << dest << " = " << val1 << " + " << val2 << " (Result: " << result << ")\n";
            printLog("ADD: " + dest + " = " + op1 + " + " + op2);
        }
        catch (...) {
            std::cerr << "[ERROR] Invalid operand in ADD: " << op1 << " or " << op2 << "\n";
            errorFlag = true;
        }
    }
    else if (instr.type == InstructionType::SUBTRACT && instr.args.size() == 3) {
        const std::string& dest = instr.args[0];
        const std::string& op1 = instr.args[1];
        const std::string& op2 = instr.args[2];

        try {
            int val1 = (memory.count(op1) ? memory[op1] : std::stoi(op1));
            int val2 = (memory.count(op2) ? memory[op2] : std::stoi(op2));
            int result = clamp_uint16(val1 - val2);
            memory[dest] = result;

            std::cout << "[INFO] SUBTRACT: " << dest << " = " << val1 << " - " << val2 << " (Result: " << result << ")\n";
            printLog("SUBTRACT: " + dest + " = " + op1 + " - " + op2);
        }
        catch (...) {
            std::cerr << "[ERROR] Invalid operand in SUBTRACT: " << op1 << " or " << op2 << "\n";
            errorFlag = true;
        }
    }
    else if (instr.type == InstructionType::FOR) {
        // FOR instruction format:
        // args: ["FOR", <instrType0>, <arg0>..., ";", ..., "REP", "<repeatCount>"]
        // We need to parse and execute instructions inside FOR repeatCount times.

        if (forContext.currentDepth == 0) {
            // Initialize FOR context
            forContext.args = instr.args;
            forContext.currentDepth = 1;
            forContext.repeatCount = 1; // default in case parsing fails
            forContext.instructionIndex = 1; // skip "FOR" marker
            forContext.executedIterations = 0;

            // Parse repeat count from the end of args
            if (forContext.args.size() >= 2) {
                try {
                    int repPos = forContext.args.size() - 2;
                    if (forContext.args[repPos] == "REP") {
                        forContext.repeatCount = std::stoi(forContext.args.back());
                    }
                }
                catch (...) {
                    forContext.repeatCount = 1;
                }
            }
        }

        // Execute instructions inside the FOR block, cycling repeatCount times
        if (forContext.executedIterations < forContext.repeatCount) {
            // Parse next instruction inside FOR context
            if (forContext.instructionIndex >= forContext.args.size() - 2) {
                // Reached end of FOR block, increment iteration count
                forContext.executedIterations++;
                forContext.instructionIndex = 1; // reset to first instruction inside FOR block
            }
            if (forContext.executedIterations < forContext.repeatCount) {
                // Extract instruction from args at instructionIndex
                // Instructions inside FOR are encoded as: [<instrType(int)>, arg0, arg1, ..., ";", ...]

                // parse instr type
                Instruction insideInstr;
                try {
                    int instrTypeInt = std::stoi(forContext.args[forContext.instructionIndex]);
                    insideInstr.type = static_cast<InstructionType>(instrTypeInt);

                    // Collect args until ";"
                    std::vector<std::string> innerArgs;
                    int idx = forContext.instructionIndex + 1;
                    while (idx < forContext.args.size() && forContext.args[idx] != ";") {
                        innerArgs.push_back(forContext.args[idx]);
                        idx++;
                    }
                    insideInstr.args = innerArgs;

                    // Update instructionIndex past this instruction
                    forContext.instructionIndex = idx + 1;

                    // Execute inner instruction immediately (recursive call might be overkill)
                    // We'll temporarily set instructions and instructionPointer to innerInstr to reuse logic
                    // Save outer state
                    auto savedInstructions = instructions;
                    size_t savedIP = instructionPointer;

                    instructions = { insideInstr };
                    instructionPointer = 0;

                    executeNextInstruction();

                    // Restore outer state
                    instructions = savedInstructions;
                    instructionPointer = savedIP;

                    // Don't advance instructionPointer in outer instructions, because FOR is one instruction

                    return; // exit early to avoid instructionPointer++
                }
                catch (...) {
                    std::cerr << "[ERROR] FOR instruction parse error.\n";
                    errorFlag = true;
                }
            }
        }
        // FOR loop done, reset context and advance
        forContext = {};  // reset FOR context struct
    }
    else {
        std::cerr << "[ERROR] Unknown or malformed instruction.\n";
        errorFlag = true;
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
        } else if (input == "process-smi") {
            executeNextInstruction();

            std::cout << "\nProcess Name:   " << getName() << "\n";
            std::cout << "Process ID:     " << getProcessId() << "\n";
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
            } else if (getStatus() != ProcessStatus::FINISHED) {
                std::cout << "\nCurrent Instruction Line: " << getCurrentInstruction() << "\n";
                std::cout << "Lines of Code:            " << getTotalInstructions() << "\n";
            }



            switch (getStatus()) {
                case ProcessStatus::READY: std::cout << "\nReady!"; break;
                case ProcessStatus::RUNNING: std::cout << "\nRunning!"; break;
                case ProcessStatus::FINISHED: std::cout << "\nFinished!"; break;
            }

            std::cout << "\n\nPress ENTER to continue...";
            std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
        } else {
            std::cout << "Unknown command. Use 'process-smi' or 'exit'.\n\n";
        }
    }

    CLIUtils::clearScreen();
    CLIUtils::printHeader();
}

void Screen::generateDummyInstructions() {
    std::lock_guard<std::mutex> lock(mtx);
    instructions.clear();

    // Declare some variables first
    instructions.push_back({ InstructionType::DECLARE, {"x", "5"} });
    instructions.push_back({ InstructionType::DECLARE, {"y", "10"} });
    instructions.push_back({ InstructionType::DECLARE, {"z", "0"} });

    // Add instruction: z = x + y
    instructions.push_back({ InstructionType::ADD, {"z", "x", "y"} });

    // Subtract instruction: x = y - x
    instructions.push_back({ InstructionType::SUBTRACT, {"x", "y", "x"} });

    // Print instruction
    instructions.push_back({ InstructionType::PRINT, {"Hello from " + name} });

    // Sleep instruction
    instructions.push_back({ InstructionType::SLEEP, {"2"} });

    // Simple FOR loop example:
    // FOR [
    //    PRINT "Looping..."
    //    ADD x x 1
    // ] REPEAT 3

    std::vector<std::string> forArgs = {
        std::to_string(static_cast<int>(InstructionType::PRINT)), "Looping...",
        ";",
        std::to_string(static_cast<int>(InstructionType::ADD)), "x", "x", "1",
        ";",
        "REP", "3"
    };
    instructions.push_back({ InstructionType::FOR, forArgs });

    instructionPointer = 0;
    status = ProcessStatus::READY;
}


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

// Getters & setters
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
