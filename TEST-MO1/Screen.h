#ifndef SCREEN_H
#define SCREEN_H

#include <string>
#include <vector>
#include <fstream>
#include <mutex>

// Enum for process status
enum class ProcessStatus {
    READY,
    RUNNING,
    FINISHED
};



// Enum for instruction types
enum class InstructionType {
    PRINT,
    SLEEP,
    INVALID
};

// Instruction struct
struct Instruction {
    InstructionType type = InstructionType::INVALID;
    std::vector<std::string> args;
};

class Screen {
public:
    // Constructors
    Screen();
    Screen(const std::string& name, const std::vector<Instruction>& instrs);

    // Accessors and mutators
    std::string getName() const;
    void setName(const std::string& newName);

    std::string getCreationTimestamp() const;
    std::string getTimestamp() const;

    size_t getCurrentInstruction() const;
    size_t getTotalInstructions() const;

    void setCoreAssigned(int core);
    int getCoreAssigned() const;

    void setStatus(ProcessStatus newStatus);
    ProcessStatus getStatus() const;
    bool isFinished() const;

    void setError(bool err = true);
    bool hasError() const;

     void advanceInstruction();
    // Operations
    void generateDummyInstructions();
    void executeNextInstruction();
    void printLog(const std::string& msg);
    void showScreen();
    void truncateInstructions(int n);

private:
    void updateTimestamp();
    void assignCoreIfUnassigned(int totalCores);

    std::string name;
    std::vector<Instruction> instructions;
    size_t instructionPointer;

    ProcessStatus status;
    int coreAssigned;

    std::string creationTimestamp;
    mutable std::mutex mtx;
    std::ofstream logFile;

    bool errorFlag = false;
};

#endif // SCREEN_H
