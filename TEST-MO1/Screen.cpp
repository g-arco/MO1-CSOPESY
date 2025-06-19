#include "Screen.h"
#include <iostream>
#include <chrono>
#include <ctime>
#include <iomanip>

// Default constructor
Screen::Screen()
    : name("default"), instructionPointer(0),
    status(ProcessStatus::READY), coreAssigned(-1)
{
    updateTimestamp();
    instructions.clear();
    // You may open a default log file here if you want, or leave closed
}

// Parameterized constructor
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

std::string Screen::getName() const {
    std::lock_guard<std::mutex> lock(mtx);
    return name;
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

void Screen::executeNextInstruction() {
    std::lock_guard<std::mutex> lock(mtx);

    if (status == ProcessStatus::FINISHED) return;
    if (instructionPointer >= instructions.size()) {
        status = ProcessStatus::FINISHED;
        printLog("Process finished execution.");
        return;
    }

    const Instruction& instr = instructions[instructionPointer];
    if (instr.type == InstructionType::PRINT && !instr.args.empty()) {
        printLog("PRINT: " + instr.args[0]);
        std::cout << "[" << name << "] " << instr.args[0] << std::endl;
    }
    else if (instr.type == InstructionType::SLEEP && !instr.args.empty()) {
        printLog("SLEEP: " + instr.args[0]);
        int sleepTime = std::stoi(instr.args[0]);
        std::this_thread::sleep_for(std::chrono::seconds(sleepTime));
    }
    // Add other instructions here if needed

    instructionPointer++;

    if (instructionPointer >= instructions.size()) {
        status = ProcessStatus::FINISHED;
        printLog("Process finished execution.");
    }
}

void Screen::printLog(const std::string& msg) {
    std::lock_guard<std::mutex> lock(mtx);
    if (logFile.is_open()) {
        logFile << "[" << creationTimestamp << "] " << msg << std::endl;
    }
}

void Screen::showScreen() {
    std::cout << "Process: " << getName() << " - Status: ";
    switch (getStatus()) {
    case ProcessStatus::READY: std::cout << "READY"; break;
    case ProcessStatus::RUNNING: std::cout << "RUNNING"; break;
    case ProcessStatus::FINISHED: std::cout << "FINISHED"; break;
    }
    std::cout << " (" << getCurrentInstruction() << "/" << getTotalInstructions() << ")\n";
}



