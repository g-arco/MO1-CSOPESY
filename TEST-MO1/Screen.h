#ifndef SCREEN_H
#define SCREEN_H

#include <string>
#include <vector>
#include <fstream>
#include "Instruction.h"
#include <mutex>
#include <thread>

enum class ProcessStatus { READY, RUNNING, FINISHED };

class Screen {
public:
    // Default constructor
    Screen();
    

    // Parameterized constructor
    Screen(const std::string& name, const std::vector<Instruction>& instructions);

    void executeNextInstruction();
    void printLog(const std::string& msg);
    void showScreen();

    std::string getName() const;
    void setName(const std::string& newName);

    std::string getCreationTimestamp() const;
    size_t getCurrentInstruction() const;
    size_t getTotalInstructions() const;
    std::string getTimestamp() const;

    void setCoreAssigned(int core);
    int getCoreAssigned() const;

    void setStatus(ProcessStatus newStatus);
    ProcessStatus getStatus() const;

    bool isFinished() const;

    void generateDummyInstructions();

private:
    mutable std::mutex mtx;

    std::string name;
    std::vector<Instruction> instructions;
    size_t instructionPointer;
    ProcessStatus status;
    std::ofstream logFile;
    std::string creationTimestamp;
    int coreAssigned;

    void updateTimestamp();
};

#endif
