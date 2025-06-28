#ifndef SCREEN_H
#define SCREEN_H

#include <string>
#include <vector>
#include <fstream>
#include <mutex>
#include <unordered_map>

struct ForExecutionContext {
    std::vector<std::string> args;
    size_t instructionIndex = 0;
    int repeatCount = 1;
    int executedIterations = 0;
    int currentDepth = 0;
};

enum class ProcessStatus {
    READY,
    RUNNING,
    FINISHED,
    SLEEPING
};

enum class InstructionType {
    PRINT,
    DECLARE,
    ADD,
    SUBTRACT,
    SLEEP,
    FOR,
    INVALID
};

struct Instruction {
    InstructionType type = InstructionType::INVALID;
    std::vector<std::string> args;
};

class Screen {
public:
    Screen();
    Screen(const std::string& name_, const std::vector<Instruction>& instrs, int id);
    void setInstructions(const std::vector<Instruction>& instrs);
    void setScheduled(bool value);
    bool isScheduled() const;

    ForExecutionContext forContext;

    void generateDummyInstructions();
    void executeNextInstruction();
    void truncateInstructions(int n);
    void showScreen();

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

    void printLog(const std::string& msg);
    int getProcessId() const;
    void setProcessId(int id);

    void setSleepUntilTick(int64_t tick);
    int64_t getSleepUntilTick() const;

private:
    void updateTimestamp();
    void assignCoreIfUnassigned(int totalCores);
    bool isNumber(const std::string& s) const;
    int resolveValue(const std::string& token);

    std::string name;
    std::vector<Instruction> instructions;
    size_t instructionPointer;

    std::unordered_map<std::string, int> memory;

    ProcessStatus status;
    int coreAssigned;

    std::string creationTimestamp;
    mutable std::mutex mtx;
    std::ofstream logFile;

    bool errorFlag = false;
    int processId = 0;

    int64_t sleepUntilTick = 0;  
};

#endif // SCREEN_H