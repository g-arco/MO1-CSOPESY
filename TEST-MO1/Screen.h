#ifndef SCREEN_H
#define SCREEN_H

#include <string>
#include <vector>
#include <fstream>
#include "Instruction.h"

enum class ProcessStatus { RUNNING, FINISHED };


class Screen {
    std::string name;
    std::vector<Instruction> instructions;
    size_t instructionPointer;
    ProcessStatus status;
    std::ofstream logFile;
    std::string creationTimestamp;
    int coreAssigned = -1;

public:
    Screen(const std::string& name, const std::vector<Instruction>& instrs);

    void executeNextInstruction();
    void printLog(const std::string& msg);
    bool isFinished() const;
    std::string getName() const;
    void showScreen();


    // New getters for CLI display
    std::string getCreationTimestamp() const { return creationTimestamp; }
    size_t getCurrentInstruction() const { return instructionPointer + 1; }
    size_t getTotalInstructions() const { return instructions.size(); }

    void setCoreAssigned(int core) { coreAssigned = core; }
    int getCoreAssigned() const { return coreAssigned; }
};

#endif
