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

// Constructor
Screen::Screen()
    : name("default"), instructionPointer(0),
      status(ProcessStatus::READY), coreAssigned(-1), errorFlag(false)
{
    updateTimestamp();
    instructions.clear();
}

Screen::Screen(const std::string& name_, const std::vector<Instruction>& instrs)
    : name(name_), instructions(instrs), instructionPointer(0),
      status(ProcessStatus::READY), coreAssigned(-1), errorFlag(false)
{
    updateTimestamp();
    logFile.open(name + ".log", std::ios::app);
    if (!logFile.is_open()) {
        std::cerr << "Failed to open log file for process: " << name << std::endl;
    }
}

void Screen::executeNextInstruction() {
    std::lock_guard<std::mutex> lock(mtx);

    assignCoreIfUnassigned(4);

    if (status == ProcessStatus::FINISHED || instructionPointer >= instructions.size()) {
        status = ProcessStatus::FINISHED;
        printLog("Process finished execution.");
        return;
    }

    status = ProcessStatus::RUNNING;
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

        std::cout << logEntry << std::endl;
    } else if (instr.type == InstructionType::SLEEP && !instr.args.empty()) {
        try {
            int duration = std::stoi(instr.args[0]);
            std::cout << "[INFO] Sleeping for " << duration << " second(s)..." << std::endl;
            std::this_thread::sleep_for(std::chrono::seconds(duration));
        } catch (...) {
            std::cerr << "[ERROR] Invalid sleep duration: " << instr.args[0] << "\n";
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

        std::cout << "========== PROCESS SCREEN ==========\n";
        std::cout << "Process Name: " << getName() << "\n";
        std::cout << "Enter a command (process-smi / exit): ";

        std::string input;
        std::getline(std::cin, input);

        if (input == "exit") {
            break;
        } else if (input == "process-smi") {
            // Don't clear screen again here

            executeNextInstruction();

            std::cout << "========== PROCESS INFO ==========\n";
            std::cout << "Process Name:   " << getName() << "\n";
            std::cout << "ID:             " << getCreationTimestamp() << "\n";
            std::cout << "Core Assigned:  " << getCoreAssigned() << "\n";

            std::cout << "\n========== LOGS ==========\n";
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

            std::cout << "\nCurrent Instruction Line: " << getCurrentInstruction() << "\n";
            std::cout << "Lines of Code:            " << getTotalInstructions() << "\n";

            std::cout << "Status:                   ";
            switch (getStatus()) {
                case ProcessStatus::READY: std::cout << "READY"; break;
                case ProcessStatus::RUNNING: std::cout << "RUNNING"; break;
                case ProcessStatus::FINISHED: std::cout << "FINISHED"; break;
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
    instructions = {
        { InstructionType::PRINT, {"Hello from " + name} },
        { InstructionType::SLEEP, {"3"} },
        { InstructionType::PRINT, {"Dummy process completed."} }
    };
    instructionPointer = 0;
    status = ProcessStatus::READY;
}

void Screen::printLog(const std::string& msg) {
    std::lock_guard<std::mutex> lock(mtx);
    if (logFile.is_open()) {
        logFile << "[" << creationTimestamp << "] " << msg << std::endl;
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
    std::strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", &localTime);
    return std::string(buffer);
}
