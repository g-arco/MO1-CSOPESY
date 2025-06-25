#include "Screen.h"
#include <iostream>
#include <chrono>
#include <ctime>
#include <iomanip>

Screen::Screen()
    : name("default"), instructionPointer(0),
      status(ProcessStatus::READY), coreAssigned(-1)
{
    updateTimestamp();
    instructions.clear();
    // Optional: Open a log file if needed
}


// Default constructor
Screen::Screen(const std::string& name_, const std::vector<Instruction>& instrs)
    : name(name_), instructions(instrs), instructionPointer(0),
      status(ProcessStatus::READY), coreAssigned(-1)
{
    updateTimestamp();
    logFile.open(name + ".log", std::ios::app);
    if (!logFile.is_open()) {
        std::cerr << "Failed to open log file for process: " << name << std::endl;
    }
}


void Screen::executeNextInstruction() {
    std::lock_guard<std::mutex> lock(mtx);

    if (status == ProcessStatus::FINISHED) return;
    if (instructionPointer >= instructions.size()) {
        status = ProcessStatus::FINISHED;
        printLog("Process finished execution.");
        return;
    }

    Instruction& instr = instructions[instructionPointer];

    if (instr.type == InstructionType::PRINT && !instr.args.empty()) {
        std::string logEntry = "[" + getTimestamp() + "] " + instr.args[0];

        // Append log to file
        std::ofstream logFile(name + ".log", std::ios::app);
        if (logFile.is_open()) {
            logFile << logEntry << "\n";
        }

        std::cout << logEntry << std::endl;
    }

    instructionPointer++;

    if (instructionPointer >= instructions.size()) {
        status = ProcessStatus::FINISHED;
        printLog("Process finished execution.");
    }
}



std::string Screen::getName() const {
    std::lock_guard<std::mutex> lock(mtx);
    return name;
}

void Screen::showScreen() {
    while (true) {
        // Clear screen
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
#ifdef _WIN32
            system("cls");
#else
            system("clear");
#endif

            std::cout << "========== PROCESS INFO ==========\n";
            std::cout << "Process Name:   " << getName() << "\n";
            std::cout << "ID:             " << getCreationTimestamp() << "\n";
            std::cout << "Core Assigned:  " << getCoreAssigned() << "\n";

            std::cout << "\n========== LOGS ==========\n";
            logFile.flush(); // Ensure all logs are written

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

            std::cout << "\n\nPress ENTER to return to command menu...";
            std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');

        } else {
            std::cout << "Unknown command. Use 'process-smi' or 'exit'.\n\n";
        }
    }
}

void Screen::setName(const std::string& newName) {
    std::lock_guard<std::mutex> lock(mtx);
    name = newName;
    // Optionally reopen log file if you want
}

void Screen::generateDummyInstructions() {
    std::lock_guard<std::mutex> lock(mtx);
    instructions.clear();

    instructions.push_back(Instruction{ InstructionType::PRINT, {"Hello from " + name} });
    instructions.push_back(Instruction{ InstructionType::SLEEP, {"3"} });
    instructions.push_back(Instruction{ InstructionType::PRINT, {"Dummy process completed."} });

    instructionPointer = 0;
    status = ProcessStatus::READY;
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


void Screen::printLog(const std::string& msg) {
    std::lock_guard<std::mutex> lock(mtx);
    if (logFile.is_open()) {
        logFile << "[" << creationTimestamp << "] " << msg << std::endl;
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