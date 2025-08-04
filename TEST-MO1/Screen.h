#ifndef SCREEN_H
#define SCREEN_H

#include <string>
#include <vector>
#include <fstream>
#include <thread>
#include <atomic>
#include <atomic>
#include <atomic>
#include <mutex>
#include <map>
#include "Config.h"

enum class ProcessStatus { READY, RUNNING, FINISHED };

// CHANGE: Enhanced instruction types to include memory operations
enum class InstructionType {
    PRINT,
    SLEEP,
    DECLARE,
    ADD,
    SUBTRACT,
    READ,    // CHANGE: New instruction type for memory read
    WRITE    // CHANGE: New instruction type for memory write
};

struct Instruction {
    InstructionType type;
    std::vector<std::string> args;
};

class Screen {
private:
    std::string name;
    std::vector<Instruction> instructions;
    size_t instructionPointer;
    ProcessStatus status;
    int coreAssigned;
    std::string creationTimestamp;
    std::ofstream logFile;
    mutable std::mutex mtx;

    // Original memory map (kept for compatibility but will be superseded)
    std::map<std::string, int> memory;

    bool isSleeping;
    int sleepRemainingTicks;
    int sleepStartTick;

    bool errorFlag;
    int processId;

    // CHANGE: New members for memory management
    int allocatedMemorySize;
    std::string errorMessage;
    std::string errorTimestamp;

    // Helper methods
    bool isNumber(const std::string& s) const;
    int resolveValue(const std::string& token);
    void assignCoreIfUnassigned(int totalCores);
    void updateTimestamp();

    bool hasMemoryError = false;
    std::map<std::string, uint16_t> variables;

public:
    Screen();
    Screen(const std::string& name_, const std::vector<Instruction>& instrs, int id);

    // Core functionality
    void executeNextInstruction();
    void advanceInstruction();
    void showScreen();
    void generateDummyInstructions(const Config& config);
    void printLog(const std::string& msg);
    std::string getStatusString() const;


    // Getters and setters
    std::string getName() const;
    void setName(const std::string& newName);
    std::string getCreationTimestamp() const;
    size_t getCurrentInstruction() const;
    size_t getTotalInstructions() const;
    void setCoreAssigned(int core);
    int getCoreAssigned() const;
    void setStatus(ProcessStatus newStatus);
    ProcessStatus getStatus() const;
    bool isFinished() const;
    void setError(bool err);
    bool hasError() const;
    void truncateInstructions(int n);
    std::string getTimestamp() const;
    int getProcessId() const;
    void setProcessId(int id) { processId = id; }
    void setInstructions(const std::vector<Instruction>& instrs);
    void setScheduled(bool value);
    bool isScheduled() const;

    // CHANGE: New methods for memory management
    void setAllocatedMemory(int size) { allocatedMemorySize = size; }
    int getAllocatedMemory() const { return allocatedMemorySize; }
    void setErrorInfo(const std::string& message, const std::string& timestamp) {
        errorMessage = message;
        errorTimestamp = timestamp;
        errorFlag = true;
    }
    std::string getErrorMessage() const { return errorMessage; }
    std::string getErrorTimestamp() const { return errorTimestamp; }

    bool checkSleepComplete();

    bool getIsSleeping() const { return isSleeping; }
    int getSleepRemainingTicks() const { return sleepRemainingTicks; }
    void setSleeping(bool sleeping) { isSleeping = sleeping; }
    void setSleepRemainingTicks(int ticks) { sleepRemainingTicks = ticks; }
    void decrementSleepTicks() { if (sleepRemainingTicks > 0) sleepRemainingTicks--; }
};

#endif