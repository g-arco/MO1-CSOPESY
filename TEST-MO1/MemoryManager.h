#ifndef MEMORYMANAGER_H
#define MEMORYMANAGER_H

#include <vector>
#include <map>
#include <mutex>
#include <string>
#include <fstream>
#include <unordered_map>
#include <memory>

// CHANGE: Added new structures for demand paging implementation
struct Page {
    int pageNumber;
    int frameNumber;
    bool inMemory;
    bool dirty;
    int lastAccessed;  // For LRU replacement

    Page() : pageNumber(-1), frameNumber(-1), inMemory(false), dirty(false), lastAccessed(0) {}
};

struct Frame {
    int frameNumber;
    int processId;
    int pageNumber;
    bool occupied;
    std::vector<uint8_t> data;  // Frame data storage

    Frame(int size) : frameNumber(-1), processId(-1), pageNumber(-1), occupied(false) {
        data.resize(size, 0);
    }
};

struct ProcessMemory {
    int processId;
    int allocatedMemory;
    std::vector<Page> pages;
    std::map<std::string, uint16_t> symbolTable;  // Variable storage (max 32 vars)
    bool hasMemoryViolation;
    std::string violationTime;
    std::string violationAddress;

    ProcessMemory() : processId(-1), allocatedMemory(0), hasMemoryViolation(false) {}
};

// Original Block structure (keeping for compatibility)
struct Block {
    int start;
    int size;
    int processId;
    bool free;
};

class MemoryManager {
private:
    // Original members (kept for compatibility)
    std::atomic<bool> _shouldStop{ false };
    bool isValidProcessMemory(const ProcessMemory& pm) const {
        return pm.processId > 0 &&
            pm.allocatedMemory > 0 &&
            pm.allocatedMemory <= maxOverallMem &&
            !pm.pages.empty();
    }
    bool isMemoryFull() const {
        int usedFrames = 0;
        for (const auto& frame : physicalFrames) {
            if (frame.occupied) usedFrames++;
        }
        return usedFrames >= (physicalFrames.size() * 0.9); // 90% threshold
    }


    std::vector<Block> memory;
    int totalMemory;
    int memPerProc;
    int frameSize;
    int nextSnapshot;

    mutable std::mutex memoryMutex;
    // CHANGE: New members for demand paging
    int maxOverallMem;
    int memPerFrame;
    int minMemPerProc;
    int maxMemPerProc;
    std::vector<Frame> physicalFrames;
    std::ofstream backingStore;
    int globalTime;  // For LRU tracking

    // Statistics tracking
    int numPagedIn;
    int numPagedOut;
    int idleCpuTicks;
    int activeCpuTicks;
    int totalCpuTicks;

    // CHANGE: Added helper methods for demand paging
    int findFreeFrame();
    int selectVictimFrame();  // LRU replacement
    
    bool isValidAddress(int processId, uint32_t address);
    int getPageNumber(uint32_t address);
    int getOffsetInPage(uint32_t address);
    void writeToBackingStore(int processId, int pageNumber, const std::vector<uint8_t>& data);
    std::vector<uint8_t> readFromBackingStore(int processId, int pageNumber);

public:
    // Original constructor (kept for compatibility)
    // MemoryManager(int total, int perProc, int frameSize);

    // CHANGE: New constructor with demand paging parameters
    MemoryManager(int maxMem, int minProc, int maxProc, int frameSize);
    ~MemoryManager();
    void finish();


    std::map<int, ProcessMemory> processMemoryMap;
    bool isProcessAllocated(int pid) const;


    // Original methods (kept for compatibility)
    bool allocate(int processId);
    void release(int processId);
    int getProcessCount() const;
    int calculateExternalFragmentation() const;
    void snapshot(int quantumCycle);

    // CHANGE: New methods for MCO2 requirements
    bool allocateProcess(int processId, int memorySize);
    void deallocateProcess(int processId);

    // Memory access operations (for READ/WRITE instructions)
    uint16_t readMemory(int processId, uint32_t address);
    void writeMemory(int processId, uint32_t address, uint16_t value);

    // Variable management (symbol table operations)
    bool declareVariable(int processId, const std::string& varName, uint16_t value);
    uint16_t getVariable(int processId, const std::string& varName);
    void setVariable(int processId, const std::string& varName, uint16_t value);

    // Memory debugging commands
    void processingSmi();
    void vmstat();

    // Statistics methods
    int getTotalMemory() const { return maxOverallMem; }
    int getUsedMemory() const;
    int getFreeMemory() const;
    int getNumPagedIn() const { return numPagedIn; }
    int getNumPagedOut() const { return numPagedOut; }
    void updateCpuTicks(int idle, int active);

    // Process memory violation tracking
    bool hasProcessViolation(int processId) const;
    std::string getViolationInfo(int processId) const;

    void pageIn(int processId, int pageNumber);
    void pageOut(int frameNumber);

    void printReport() const;
};

// CHANGE: Global memory manager declaration
extern std::unique_ptr<MemoryManager> memoryManager;

#endif