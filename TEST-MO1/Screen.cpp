#include "Screen.h"
#include "CLIUtils.h"
#include <iostream>
#include <chrono>
#include <ctime>
#include <thread>


static std::string getCurrentTime() {
    auto now = std::chrono::system_clock::now();
    std::time_t now_time = std::chrono::system_clock::to_time_t(now);
    char buffer[80];
    std::tm timeinfo;
    localtime_s(&timeinfo, &now_time); // Windows-safe
    std::strftime(buffer, sizeof(buffer), "%m/%d/%Y, %I:%M:%S %p", &timeinfo);
    return std::string(buffer);
}

Screen::Screen(const std::string& name, const std::vector<Instruction>& instrs)
    : name(name), instructions(instrs), instructionPointer(0), status(ProcessStatus::RUNNING),
    logFile(name + ".txt"), creationTimestamp(getCurrentTime()) {
    logFile << "Process: " << name << "\n";
}

void Screen::executeNextInstruction() {
    if (instructionPointer >= instructions.size()) {
        status = ProcessStatus::FINISHED;
        return;
    }

    Instruction ins = instructions[instructionPointer++];
    switch (ins.type) {
    case InstructionType::PRINT:
        printLog(ins.args[0]);
        break;
    default:
        break;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
}

void Screen::printLog(const std::string& msg) {
    logFile << "LOG: " << msg << "\n";
}

bool Screen::isFinished() const {
    return status == ProcessStatus::FINISHED;
}

std::string Screen::getName() const {
    return name;
}

void Screen::showScreen() {
    CLIUtils::clearScreen();
    std::cout << "Attached to process: " << name << "\n";
    if (isFinished()) {
        std::cout << "Process has finished.\n";
        return;
    }

    std::string cmd;
    while (true) {
        std::cout << "[screen:" << name << "]$ ";
        std::getline(std::cin, cmd);
        if (cmd == "process-smi") {
            std::cout << "Process: " << name << " [Running]\n";
        }
        else if (cmd == "exit") {
            break;
        }
        else {
            std::cout << "Unknown command.\n";
        }
    }
}
