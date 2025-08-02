#ifndef PROCESSMANAGER_H
#define PROCESSMANAGER_H

#include "Screen.h"
#include "Config.h"
#include <map>
#include <string>
#include <memory>
#include <mutex>
#include <vector>

// Forward declarations
class Scheduler;
class Screen;
struct Config;
struct Instruction;
enum class InstructionType;

class ProcessManager {
private:
    // Process storage
    static std::map<std::string, std::shared_ptr<Screen>> processes;
    static std::mutex processMutex;
    static Scheduler* scheduler;

    // Helper methods
    static std::vector<Instruction> parseInstructionString(const std::string& instructionString);

public:
    // Scheduler management
    static void setScheduler(Scheduler* sched);

    // ORIGINAL METHODS (from MO1)
    // Process creation and management
    static void createAndAttach(const std::string& name, const Config& config);
    static void registerProcess(const std::shared_ptr<Screen>& screen);
    static bool hasProcess(const std::string& name);
    static std::shared_ptr<Screen> getProcess(const std::string& name);
    static void removeProcess(const std::string& name);

    // Process listing and reporting
    static void listScreens(const Config& config);
    static void generateReport();

    // Memory operations (original - may need updating for MCO2)
    static void swapOut(const std::string& name);
    static void swapIn(const std::string& name);

    // ORIGINAL reporting methods (commented out - replaced by MemoryManager)
    // static void smiReport();
    // static void vmstatReport();

    // NEW METHODS FOR MCO2
    // Enhanced process creation with memory management
    static void createProcessWithMemory(const std::string& name, int memorySize, const Config& config);
    static void createProcessWithInstructions(const std::string& name, int memorySize,
        const std::string& instructionString, const Config& config);

    // Process information
    static void showProcessInfo(const std::string& name);
    static void showAllProcesses();

    // Statistics and monitoring
    static int getProcessCount();
    static int getRunningProcessCount();
    static int getWaitingProcessCount();
    static int getFinishedProcessCount();

    // Memory-related process management
    static std::vector<std::shared_ptr<Screen>> getProcessesInMemory();
    static std::vector<std::shared_ptr<Screen>> getProcessesInBacking();
    static void forceKillProcess(const std::string& name);

    // Process status utilities
    static void updateProcessStatuses();
    static void cleanFinishedProcesses();

    // Debug and utility functions
    static void dumpProcessList();
    static void validateProcessStates();
};

#endif // PROCESSMANAGER_H