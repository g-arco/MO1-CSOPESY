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

        /*std::cout << logEntry << std::endl;*/
    }
    else if (instr.type == InstructionType::SLEEP && !instr.args.empty()) {
        try {
            int duration = std::stoi(instr.args[0]);
            /*std::cout << "[INFO] Sleeping for " << duration << " second(s)..." << std::endl;*/
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
            int value = std::stoi(instr.args[1]);
            memory[varName] = value;
            /*std::cout << "[INFO] DECLARE: " << varName << " = " << value << std::endl;*/
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

            if (!memory.count(var1)) memory[var1] = 0;

            int result = op1 + op2;

            /*std::cout << "[INFO] ADD: " << var1 << " = " << op1 << " + " << op2
                << " (New: " << memory[var1] << ")\n";*/
            printLog("ADD " + var1 + " = " + std::to_string(op1) + " + " + std::to_string(op2));
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

            if (!memory.count(var1)) memory[var1] = 0;

            int result = op1 - op2;

            /*std::cout << "[INFO] SUBTRACT: " << var1 << " = " << op1 << " - " << op2
                << " (New: " << memory[var1] << ")\n";*/
            printLog("SUBTRACT " + var1 + " = " + std::to_string(op1) + " - " + std::to_string(op2));
        }
        catch (const std::exception& e) {
            std::cerr << "[ERROR] Invalid SUBTRACT operands: " << e.what() << "\n";
            errorFlag = true;
        }
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
            case ProcessStatus::FINISHED: std::cout << "\nFinished!"; break;
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
        default: break;
        }

        return instr;
        };

    for (int i = 0; i < count; ++i) {
        int choice = rand() % 5;  // 0�4 only
        InstructionType type = static_cast<InstructionType>(choice);
        instrs.push_back(generateSimpleInstruction(type));
    }

    instructions = instrs;
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
