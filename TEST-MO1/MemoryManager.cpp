#include "MemoryManager.h"
#include "ProcessManager.h"

#include <fstream>
#include <sstream>
#include <iomanip>
#include <ctime>
#include <set>
#include <iostream>
#include <algorithm>
#include <cstring>
#include <vector>

// Original global declaration (commented out)
// MemoryManager memoryManager(16384, 4096, 16); // default config

// CHANGE: New constructor for demand paging
MemoryManager::MemoryManager(int maxMem, int minProc, int maxProc, int frameSize)
    : maxOverallMem(maxMem), minMemPerProc(minProc), maxMemPerProc(maxProc),
    memPerFrame(frameSize), globalTime(0), numPagedIn(0), numPagedOut(0),
    // Original members (kept for compatibility)
    totalMemory(maxMem), memPerProc(minProc), frameSize(frameSize), nextSnapshot(0),
    globalTickCounter(0), totalIdleTicks(0), totalActiveTicks(0), numCoresAllocated(0)
{
    // CHANGE: Initialize physical frames for demand paging
    int numFrames = maxOverallMem / memPerFrame;
    physicalFrames.reserve(numFrames);
    for (int i = 0; i < numFrames; ++i) {
        Frame frame(memPerFrame);
        frame.frameNumber = i;
        physicalFrames.push_back(frame);
    }

    // CHANGE: Initialize backing store file
    backingStore.open("csopesy-backing-store.txt", std::ios::binary | std::ios::out | std::ios::trunc);
    if (!backingStore.is_open()) {
        std::cerr << "[ERROR] Failed to create backing store file.\n";
    }

    // Original initialization (kept for compatibility)
    // memory.push_back({ 0, totalMemory, -1, true }); // COMMENTED OUT
    std::cout << "[INIT] Memory initialized with " << numFrames << " frames of " << memPerFrame << " bytes each\n";
    std::cout << "[INIT] Total memory: " << maxOverallMem << " bytes\n";

    initializeCores(4);

}

MemoryManager::~MemoryManager() {
    if (backingStore.is_open()) {
        backingStore.close();
    }
}

void MemoryManager::initializeCores(int numCores) {
    std::lock_guard<std::mutex> lock(tickMutex);

    // Allocate arrays of atomics instead of using vectors
    coreActivity = std::make_unique<std::atomic<bool>[]>(numCores);
    perCoreIdleTicks = std::make_unique<std::atomic<int>[]>(numCores);
    perCoreActiveTicks = std::make_unique<std::atomic<int>[]>(numCores);
    numCoresAllocated = numCores;

    // Initialize the atomic values
    for (int i = 0; i < numCores; ++i) {
        coreActivity[i].store(false);
        perCoreIdleTicks[i].store(0);
        perCoreActiveTicks[i].store(0);
    }
    //std::cout << "[MEMORY] Initialized tick tracking for " << numCores << " cores\n";
}


void MemoryManager::recordCoreActivity(int coreId, bool isActive, int ticks) {
    if (coreId < 0 || coreId >= numCoresAllocated) {
        return; // Invalid core ID
    }

    // CRITICAL: Always update global tick counter first
    globalTickCounter.fetch_add(ticks);

    // Update per-core statistics
    if (isActive) {
        perCoreActiveTicks[coreId].fetch_add(ticks);
        totalActiveTicks.fetch_add(ticks);
    }
    else {
        perCoreIdleTicks[coreId].fetch_add(ticks);
        totalIdleTicks.fetch_add(ticks);
    }

    // Update core activity status
    coreActivity[coreId].store(isActive);


    /*DEBUG: Print every 50 ticks to verify it's working
    static std::atomic<int> debugCounter{ 0 };
    if (debugCounter.fetch_add(1) % 50 == 0) {
        int totalTicks = getTotalTicks();
        int activeTicks = getTotalActiveTicks();
        int idleTicks = getTotalIdleTicks();

        std::cout << "\n=== CPU UTILIZATION DEBUG ===\n";
        std::cout << "Current tick:     " << getCurrentTick() << "\n";
        std::cout << "Total ticks:      " << totalTicks << "\n";
        std::cout << "Active ticks:     " << activeTicks << "\n";
        std::cout << "Idle ticks:       " << idleTicks << "\n";

        if (totalTicks > 0) {
            double utilization = (double)activeTicks / totalTicks * 100.0;
            std::cout << "CPU utilization:  " << std::fixed << std::setprecision(2)
                << utilization << "%\n";
        }
        else {
            std::cout << "CPU utilization:  N/A (no ticks recorded)\n";
        }
        std::cout << "============================\n\n";
    }*/


}



void MemoryManager::incrementGlobalTick(int ticks) {
    globalTickCounter.fetch_add(ticks);
}

void MemoryManager::initializeBackingStorePages(int processId) {
    std::vector<uint8_t> emptyPage(memPerFrame, 0);
    auto it = processMemoryMap.find(processId);
    if (it == processMemoryMap.end()) return;
    int numPages = it->second.pages.size();
    for (int i = 0; i < numPages; ++i) {
        writeToBackingStore(processId, i, emptyPage);
    }
}


// CHANGE: New method to allocate process with specific memory size
bool MemoryManager::allocateProcess(int processId, int memorySize) {
    std::lock_guard<std::recursive_mutex> lock(memoryMutex);

    if (processMemoryMap.find(processId) != processMemoryMap.end()) {
        // Already allocated
        return false;
    }

    int currentUsed = getUsedMemory();
    if (currentUsed + memorySize > maxOverallMem) {
        //std::cout << "Insufficient system memory for allocation\n";
        return false;
    }

    // Validate memory size (must be power of 2, within range)
    if (memorySize < 64 || memorySize > 65536) {
        std::cout << "Invalid memory allocation: size must be between 64 and 65536 bytes\n";
        return false;
    }

    // Check if it's a power of 2
    if ((memorySize & (memorySize - 1)) != 0) {
        std::cout << "Invalid memory allocation: size must be a power of 2\n";
        return false;
    }

    ProcessMemory procMem;
    procMem.processId = processId;
    procMem.allocatedMemory = memorySize;

    // Calculate number of pages needed
    int frameSize = memPerFrame; // or whatever your frame size is
    int numPages = (memorySize + frameSize - 1) / frameSize;

    if (numPages == 0) {
        numPages = 1;  // You probably have this fix, but let's verify
    }

    procMem.pages.clear();
    procMem.pages.resize(numPages);

    for (int i = 0; i < numPages; i++) {
        procMem.pages[i].frameNumber = -1; // no frame assigned yet
        procMem.pages[i].inMemory = false; // not yet loaded in memory
    }

    procMem.hasMemoryViolation = false;
    procMem.violationTime = "";


    processMemoryMap[processId] = procMem;
    initializeBackingStorePages(processId);


    // now use `pm`, which is a copy


   // std::cout << "[MEMORY] Allocated " << memorySize << " bytes (" << numPages << " pages) for process " << processId << "\n";
    return true;
}


// CHANGE: New method to deallocate process memory
void MemoryManager::deallocateProcess(int processId) {
    std::lock_guard<std::recursive_mutex> lock(memoryMutex);

    auto it = processMemoryMap.find(processId);
    if (it == processMemoryMap.end()) {
        return;  // Process not found, return early
    }

    ProcessMemory& procMem = it->second;

    // Free all frames used by this process
    for (auto& page : procMem.pages) {
        if (page.inMemory && page.frameNumber >= 0 && page.frameNumber < physicalFrames.size()) {
            physicalFrames[page.frameNumber].occupied = false;
            physicalFrames[page.frameNumber].processId = -1;
            physicalFrames[page.frameNumber].pageNumber = -1;
        }
    }


    // Clear symbol table
    procMem.symbolTable.clear();

    processMemoryMap.erase(it);


    //std::cout << "[MEMORY] Deallocated memory for process " << processId << "\n";
}

// CHANGE: Demand paging - find free frame
int MemoryManager::findFreeFrame() {
    for (int i = 0; i < physicalFrames.size(); ++i) {
        if (!physicalFrames[i].occupied) {
            return i;
        }
    }
    return -1; // No free frame
}

// CHANGE: LRU page replacement algorithm
int MemoryManager::selectVictimFrame() {
    int victimFrame = 0;
    int oldestTime = physicalFrames[0].frameNumber >= 0 ?
        processMemoryMap[physicalFrames[0].processId].pages[physicalFrames[0].pageNumber].lastAccessed : globalTime;

    for (int i = 0; i < physicalFrames.size(); ++i) {
        if (physicalFrames[i].occupied) {
            int processId = physicalFrames[i].processId;
            int pageNumber = physicalFrames[i].pageNumber;

            auto it = processMemoryMap.find(processId);
            if (it != processMemoryMap.end() && pageNumber < it->second.pages.size()) {
                int lastAccessed = it->second.pages[pageNumber].lastAccessed;
                if (lastAccessed < oldestTime) {
                    oldestTime = lastAccessed;
                    victimFrame = i;
                }
            }
            else {
                // Process not found or invalid page - good candidate for eviction
                return i;
            }
        }
    }

    // If no victim found, just use first occupied frame
    if (victimFrame == -1) {
        for (int i = 0; i < physicalFrames.size(); ++i) {
            if (physicalFrames[i].occupied) {
                return i;
            }
        }
    }

    return victimFrame;
}

// CHANGE: Page in operation
void MemoryManager::pageIn(int processId, int pageNumber) {
    std::lock_guard<std::recursive_mutex> lock(memoryMutex);

    int frameNumber = findFreeFrame();

    // In pageIn(), ensure proper eviction when memory is full
    if (frameNumber == -1) {
        frameNumber = selectVictimFrame();
        if (frameNumber >= 0) {
            pageOut(frameNumber); // This should increment numPagedOut
        }
        else {
            throw std::runtime_error("No frames available for paging");
        }
    }

    if (frameNumber < 0 || frameNumber >= physicalFrames.size()) {
        throw std::runtime_error("Invalid frame number: " + std::to_string(frameNumber));
    }

    // Read page from backing store
    std::vector<uint8_t> pageData = readFromBackingStore(processId, pageNumber);

    // Load into frame
    physicalFrames[frameNumber].occupied = true;
    physicalFrames[frameNumber].processId = processId;
    physicalFrames[frameNumber].pageNumber = pageNumber;
    physicalFrames[frameNumber].data = pageData;

    // Update page table
    auto it = processMemoryMap.find(processId);
    if (it != processMemoryMap.end() && pageNumber < it->second.pages.size()) {
        it->second.pages[pageNumber].inMemory = true;
        it->second.pages[pageNumber].frameNumber = frameNumber;
        it->second.pages[pageNumber].lastAccessed = ++globalTime;
    }

    numPagedIn++;

    // CHANGE 11: Force more paging when memory is full
    // ENHANCED: More aggressive paging policy
    // Force pageouts when memory utilization is above 75%
    int occupiedFrames = 0;
    for (const auto& frame : physicalFrames) {
        if (frame.occupied) occupiedFrames++;
    }

    double utilization = (double)occupiedFrames / physicalFrames.size();
    if (utilization > 0.95) { // 90% threshold instead of 100%
        // Find a different frame to page out (not the one we just paged in)
        for (int i = 0; i < physicalFrames.size(); ++i) {
            if (i != frameNumber && physicalFrames[i].occupied) {
                // Check if this frame belongs to a different process or older page
                if (physicalFrames[i].processId != processId) {
                  //  std::cout << "[PAGING] Proactive pageout due to high memory utilization ("
                    //    << (utilization * 100) << "%)\n";
                    pageOut(i);
                    break;
                }
            }
        }
    }
  // std::cout << "[PAGING] Paged in: Process " << processId << ", Page " << pageNumber << " -> Frame " << frameNumber << "\n";
}

// CHANGE: Page out operation
void MemoryManager::pageOut(int frameNumber) {
    std::lock_guard<std::recursive_mutex> lock(memoryMutex);

    Frame& frame = physicalFrames[frameNumber];
    if (!frame.occupied) return;

    int processId = frame.processId;
    int pageNumber = frame.pageNumber;

    // Find the process in the map
    auto it = processMemoryMap.find(processId);
    if (it == processMemoryMap.end()) {
        // Process not found, just free the frame
        frame.occupied = false;
        frame.processId = -1;
        frame.pageNumber = -1;
        return;
    }

    ProcessMemory& procMem = it->second;

    // Write to backing store if dirty
    if (pageNumber < procMem.pages.size() && procMem.pages[pageNumber].dirty) {
        writeToBackingStore(processId, pageNumber, frame.data);
    }

    // Update page table
    if (pageNumber < procMem.pages.size()) {
        procMem.pages[pageNumber].inMemory = false;
        procMem.pages[pageNumber].frameNumber = -1;
    }

    // Free frame
    frame.occupied = false;
    frame.processId = -1;
    frame.pageNumber = -1;

    numPagedOut++;
   // std::cout << "[PAGING] Paged out: Process " << processId << ", Page " << pageNumber << " from Frame " << frameNumber << "\n";
}

// CHANGE: Memory access validation
bool MemoryManager::isValidAddress(int processId, uint32_t address) {
    auto it = processMemoryMap.find(processId);
    if (it == processMemoryMap.end()) return false;

    return address < it->second.allocatedMemory;
}

// CHANGE: Get page number from address
int MemoryManager::getPageNumber(uint32_t address) {
    return address / memPerFrame;
}

// CHANGE: Get offset within page
int MemoryManager::getOffsetInPage(uint32_t address) {
    return address % memPerFrame;
}

uint16_t MemoryManager::readMemory(int processId, uint32_t address) {
    std::lock_guard<std::recursive_mutex> lock(memoryMutex);  // ACQUIRE LOCK
    return readMemoryInternal(processId, address);  // Call internal method
}

void MemoryManager::writeMemory(int processId, uint32_t address, uint16_t value) {
    std::lock_guard<std::recursive_mutex> lock(memoryMutex);  // ACQUIRE LOCK
    writeMemoryInternal(processId, address, value); // Call internal method
}

bool MemoryManager::declareVariable(int processId, const std::string& varName, uint16_t value) {
    std::lock_guard<std::recursive_mutex> lock(memoryMutex);  // ACQUIRE LOCK
    return declareVariableInternal(processId, varName, value);
}

uint16_t MemoryManager::readMemoryInternal(int processId, uint32_t address) {
   // std::cout << "[READ] PID " << processId << " reading from 0x"
     //   << std::hex << address << std::dec << "\n";

    auto it = processMemoryMap.find(processId);
    if (it == processMemoryMap.end()) {
      //  std::cerr << "[READ] ERROR: Process " << processId << " not found in memory manager.\n";
       throw std::runtime_error("Process not found in memory manager");
    }

    if (address >= it->second.allocatedMemory) {
      //  std::cerr << "[READ] MEMORY VIOLATION: PID " << processId
        //    << " tried to read 0x" << std::hex << address << std::dec << "\n";

        auto& procMem = it->second;
        procMem.hasMemoryViolation = true;

        auto now = std::time(nullptr);
        std::tm tm_now{};
        char buf[100];
#ifdef _WIN32
        localtime_s(&tm_now, &now);
#else
        localtime_r(&now, &tm_now);
#endif
        std::strftime(buf, sizeof(buf), "%H:%M:%S", &tm_now);
        procMem.violationTime = buf;

        std::ostringstream oss;
        oss << "0x" << std::hex << address;
        procMem.violationAddress = oss.str();

       throw std::runtime_error("Memory access violation at address " + procMem.violationAddress);
    }

    int pageNumber = getPageNumber(address);
    int offset = getOffsetInPage(address);
    ProcessMemory& procMem = it->second;

    if (!procMem.pages[pageNumber].inMemory) {
       // std::cout << "[PAGE FAULT] PID " << processId << " reading page " << pageNumber
         //  << " (addr 0x" << std::hex << address << std::dec << ")\n";
        pageIn(processId, pageNumber);

        if (!procMem.pages[pageNumber].inMemory) {
           // std::cerr << "[READ] ERROR: Page " << pageNumber << " still not in memory after pageIn.\n";
            throw std::runtime_error("Failed to bring page into memory after page fault");
        }
    }

    procMem.pages[pageNumber].lastAccessed = ++globalTime;
    int frameNumber = procMem.pages[pageNumber].frameNumber;
    Frame& frame = physicalFrames[frameNumber];

    uint16_t value = 0;
    if (offset + 1 < frame.data.size()) {
        value = (frame.data[offset + 1] << 8) | frame.data[offset];
    }

  //  std::cout << "[READ] PID " << processId << " value at 0x"
    //    << std::hex << address << ": " << value << std::dec << "\n";

    return value;
}

void MemoryManager::writeMemoryInternal(int processId, uint32_t address, uint16_t value) {
  //  std::cout << "[WRITE] PID " << processId << " writing " << value << " to 0x"
    //    << std::hex << address << std::dec << "\n";

    auto it = processMemoryMap.find(processId);
    if (it == processMemoryMap.end()) {
       // std::cerr << "[WRITE] ERROR: Process " << processId << " not found in memory manager.\n";
       throw std::runtime_error("Process not found in memory manager");
    }

    if (address >= it->second.allocatedMemory) {
       // std::cerr << "[WRITE] MEMORY VIOLATION: PID " << processId
         //   << " tried to write 0x" << std::hex << address << std::dec << "\n";

        auto& procMem = it->second;
        procMem.hasMemoryViolation = true;

        auto now = std::time(nullptr);
        std::tm tm_now{};
        char buf[100];
#ifdef _WIN32
        localtime_s(&tm_now, &now);
#else
        localtime_r(&now, &tm_now);
#endif
        std::strftime(buf, sizeof(buf), "%H:%M:%S", &tm_now);
        procMem.violationTime = buf;

        std::ostringstream oss;
        oss << "0x" << std::hex << address;
        procMem.violationAddress = oss.str();

      throw std::runtime_error("Memory access violation at address " + procMem.violationAddress);
    }

    int pageNumber = getPageNumber(address);
    int offset = getOffsetInPage(address);
    ProcessMemory& procMem = it->second;

    if (!procMem.pages[pageNumber].inMemory) {
        //std::cout << "[PAGE FAULT] PID " << processId << " writing page " << pageNumber
          //  << " (addr 0x" << std::hex << address << std::dec << ")\n";
        pageIn(processId, pageNumber);

        if (!procMem.pages[pageNumber].inMemory) {
          //  std::cerr << "[WRITE] ERROR: Page " << pageNumber << " still not in memory after pageIn.\n";
            throw std::runtime_error("Failed to bring page into memory after page fault");
        }
    }

    procMem.pages[pageNumber].lastAccessed = ++globalTime;
    procMem.pages[pageNumber].dirty = true;

    int frameNumber = procMem.pages[pageNumber].frameNumber;
    Frame& frame = physicalFrames[frameNumber];

    if (offset + 1 < frame.data.size()) {
        frame.data[offset] = value & 0xFF;
        frame.data[offset + 1] = (value >> 8) & 0xFF;

      //  std::cout << "[WRITE] PID " << processId << " wrote " << value
        //    << " to page " << pageNumber << ", offset " << offset
          //  << ", frame " << frameNumber << "\n";
    }
}

// CHANGE: Variable management methods
bool MemoryManager::declareVariableInternal(int processId, const std::string& varName, uint16_t value) {

    auto it = processMemoryMap.find(processId);
    if (it == processMemoryMap.end()) {
        return false;  // Return early if process not found
    }

    ProcessMemory& procMem = it->second;

    // Check if symbol table is full (max 32 variables)
    if (procMem.symbolTable.size() >= 32) {
        //  std::cout << "[ERROR] Symbol table full. Cannot declare more variables.\n";
        return false;
    }

    // If page 0 is not in memory, we need to page it in
    // But we need to be careful about the mutex
    if (!procMem.pages.empty() && !procMem.pages[0].inMemory) {
        // We'll handle the page fault, but keep the mutex locked
        // since pageIn should also be thread-safe
        pageIn(processId, 0);

        // Re-check after pageIn
        if (!procMem.pages[0].inMemory) {
            return false;
        }
    }

    procMem.symbolTable[varName] = value;
    return true;
}

uint16_t MemoryManager::getVariable(int processId, const std::string& varName) {
    std::lock_guard<std::recursive_mutex> lock(memoryMutex);

    auto it = processMemoryMap.find(processId);
    if (it == processMemoryMap.end()) {
        return 0;  // Return early if process not found
    }

    ProcessMemory& procMem = it->second;

    if (!procMem.pages.empty() && !procMem.pages[0].inMemory) {
        // Handle page fault while keeping mutex locked
        pageIn(processId, 0);
    }

    auto& symbolTable = procMem.symbolTable;
    auto varIt = symbolTable.find(varName);
    return (varIt != symbolTable.end()) ? varIt->second : 0;
}


void MemoryManager::setVariable(int processId, const std::string& varName, uint16_t value) {
    auto it = processMemoryMap.find(processId);
    if (it != processMemoryMap.end()) {
        it->second.symbolTable[varName] = value;
    }
}

// CHANGE: Backing store operations
void MemoryManager::writeToBackingStore(int processId, int pageNumber, const std::vector<uint8_t>& data) {
    if (!backingStore.is_open()) {
        backingStore.open("csopesy-backing-store.txt", std::ios::binary | std::ios::out | std::ios::app);
    }

    // Write process ID, page number, and data
    backingStore.write(reinterpret_cast<const char*>(&processId), sizeof(processId));
    backingStore.write(reinterpret_cast<const char*>(&pageNumber), sizeof(pageNumber));
    backingStore.write(reinterpret_cast<const char*>(data.data()), data.size());
    backingStore.flush();
}

std::vector<uint8_t> MemoryManager::readFromBackingStore(int processId, int pageNumber) {
    std::vector<uint8_t> data(memPerFrame, 0);

    std::ifstream readStore("csopesy-backing-store.txt", std::ios::binary);
    if (!readStore.is_open()) {
        return data; // Return zeroed data if backing store doesn't exist
    }

    int storedPid, storedPage;
    while (readStore.read(reinterpret_cast<char*>(&storedPid), sizeof(storedPid)) &&
        readStore.read(reinterpret_cast<char*>(&storedPage), sizeof(storedPage))) {

        if (storedPid == processId && storedPage == pageNumber) {
            readStore.read(reinterpret_cast<char*>(data.data()), data.size());
            break;
        }
        else {
            // Skip this page's data
            readStore.seekg(memPerFrame, std::ios::cur);
        }
    }

    readStore.close();
    return data;
}

// CHANGE: Memory debugging - process-smi command
void MemoryManager::processingSmi() {
    std::lock_guard<std::recursive_mutex> lock(memoryMutex); // Single mutex

    std::cout << "=========================================================================================\n";
    std::cout << "| PROCESS-SMI 24.6.1       Driver Version: 12.0.1    CUDA Version: N/A              |\n";
    std::cout << "=========================================================================================\n";

    int usedMemory = getUsedMemory();
    std::cout << "| Memory Usage: " << usedMemory << " / " << maxOverallMem << " bytes";
    std::cout << std::setw(50 - std::to_string(usedMemory).length() - std::to_string(maxOverallMem).length()) << "|\n";
    std::cout << "=========================================================================================\n";
    std::cout << "| Processes:                                                                             |\n";
    std::cout << "=========================================================================================\n";
    std::cout << "| PID   Memory Usage   Process Name                                                     |\n";
    std::cout << "=========================================================================================\n";

    // CHANGE 6: Safe iteration
    for (const auto& pair : processMemoryMap) {
        if (!isValidProcessMemory(pair.second)) {
            continue; // Skip invalid entries
        }

        const ProcessMemory& procMem = pair.second;
        std::string processName = "Process" + std::to_string(procMem.processId);
        std::cout << "| " << std::setw(5) << procMem.processId
            << "   " << std::setw(12) << procMem.allocatedMemory << " bytes"
            << "   " << std::left << std::setw(45) << processName << "|\n";
    }

    std::cout << "=========================================================================================\n\n";

}


// CHANGE: Memory debugging - vmstat command
void MemoryManager::vmstat(double cpuUtilization) {
    std::lock_guard<std::recursive_mutex> lock(memoryMutex); // Single mutex

    // Read all data into local variables while the lock is held
    int localTotalMemory = maxOverallMem;
    int localUsedMemory = getUsedMemory();
    int localFreeMemory = getFreeMemory();
    int localIdleTicks = getTotalIdleTicks();
    int localActiveTicks = getTotalActiveTicks();
    int localTotalTicks = getTotalTicks();
    int localCurrentTick = getCurrentTick();
    int localPagedIn = numPagedIn;
    int localPagedOut = numPagedOut;

    std::vector<ProcessMemory> validProcesses;
    for (const auto& pair : processMemoryMap) {
        if (isValidProcessMemory(pair.second)) {
            validProcesses.push_back(pair.second);
        }
    }

    std::cout << "\nMemory Statistics:\n";
    std::cout << "====================\n";
    std::cout << "Total memory:     " << localTotalMemory << " bytes\n";
    std::cout << "Used memory:      " << localUsedMemory << " bytes\n";
    std::cout << "Free memory:      " << localFreeMemory << " bytes\n";

    std::cout << "\nCPU Statistics:\n";
    std::cout << "====================\n";
    std::cout << "Idle cpu ticks:   " << localIdleTicks << "\n";
    std::cout << "Active cpu ticks: " << localActiveTicks << "\n";
    std::cout << "Total cpu ticks:  " << localTotalTicks << "\n";
    std::cout << "Current tick:     " << localCurrentTick << "\n";

    // IMPROVED: Add CPU utilization percentage


    int totalTicks = getTotalTicks();
    if (totalTicks > 0) {
        double utilization = (double)localActiveTicks / totalTicks * 100;
        std::cout << "CPU utilization:  " << std::fixed << std::setprecision(2)
            << cpuUtilization << "%\n";
    }

    std::cout << "\nPaging Statistics:\n";
    std::cout << "====================\n";
    std::cout << "Num paged in:     " << localPagedIn << "\n";
    std::cout << "Num paged out:    " << localPagedOut << "\n";

    std::cout << "\nProcess Information:\n";
    std::cout << "====================\n";


    for (const auto& procMem : validProcesses) {
        std::cout << "Process " << procMem.processId << ":\n";
        std::cout << "  Allocated: " << procMem.allocatedMemory << " bytes\n";

        int pagesInMem = 0;
        for (const auto& page : procMem.pages) {
            if (page.inMemory) pagesInMem++;
        }

        std::cout << "  Pages in memory: " << pagesInMem << " / " << procMem.pages.size() << "\n";

        if (procMem.hasMemoryViolation) {
            std::cout << "  ** MEMORY VIOLATION at " << procMem.violationTime
                << " - Address " << procMem.violationAddress << " **\n";
        }
    }
    std::cout << "\n";

    std::cout << "\n";
}

// CHANGE: Helper methods for statistics
int MemoryManager::getUsedMemory() const {
    int used = 0;

    // Count occupied frames instead of allocated process memory
    for (const auto& frame : physicalFrames) {
        if (frame.occupied) {
            used += memPerFrame;
        }
    }

    return used;
}

int MemoryManager::getFreeMemory() const {
    return maxOverallMem - getUsedMemory();
}

void MemoryManager::updateCpuTicks(int idle, int active) {
   

    // Update new atomic counters
    totalIdleTicks.fetch_add(idle);
    totalActiveTicks.fetch_add(active);
    globalTickCounter.fetch_add(idle + active);

    // REMOVED: The problematic debug output that was causing garbled messages
    // std::cerr << "[TICKS] Active Tick: " << activeCpuTicks << ".\n";
    // std::cerr << "[TICKS] Total Tick: " << totalCpuTicks << ".\n";

    std::cout << "Current tick:     " << getCurrentTick() << "\n";

    // IMPROVED: Add CPU utilization percentage
    int totalTicks = getTotalTicks();
    if (totalTicks > 0) {
        double utilization = (double)getTotalActiveTicks() / totalTicks * 100.0;
        std::cout << "CPU utilization:  " << std::fixed << std::setprecision(2)
            << utilization << "%\n";
    }
}

// CHANGE: Memory violation tracking
bool MemoryManager::hasProcessViolation(int processId) const {
    auto it = processMemoryMap.find(processId);
    return (it != processMemoryMap.end()) ? it->second.hasMemoryViolation : false;
}

std::string MemoryManager::getViolationInfo(int processId) const {
    auto it = processMemoryMap.find(processId);
    if (it != processMemoryMap.end() && it->second.hasMemoryViolation) {
        return "shut down due to memory access violation error that occurred at " +
            it->second.violationTime + ". " + it->second.violationAddress + " invalid.";
    }
    return "";
}

// Original methods (kept for compatibility but commented out some functionality)
bool MemoryManager::allocate(int processId) {
    // CHANGE: Redirect to new allocation method with default memory size
    return allocateProcess(processId, minMemPerProc);

}

void MemoryManager::release(int processId) {
    // CHANGE: Use new deallocation method
    deallocateProcess(processId);

    /*
    // Original implementation (commented out)
    //  std::cout << "[RELEASE] Releasing memory for P" << processId << "\n";
    for (auto& block : memory) {
        if (block.processId == processId) {
            block.free = true;
            block.processId = -1;
            //std::cout << "[RELEASE] Marked block as free: start=" << block.start << ", size=" << block.size << "\n";
        }
    }

    // Merge adjacent free blocks
    for (size_t i = 0; i + 1 < memory.size(); ) {
        if (memory[i].free && memory[i + 1].free) {
        //    std::cout << "[MERGE] Merging blocks at index " << i << " and " << (i + 1) << "\n";
            memory[i].size += memory[i + 1].size;
            memory.erase(memory.begin() + i + 1);
        }
        else {
            ++i;
        }
    }
    */
}

int MemoryManager::getProcessCount() const {
    // CHANGE: Use new process memory map
    return static_cast<int>(processMemoryMap.size());

    /*
    // Original implementation (commented out)
    std::set<int> seen;
    for (const auto& block : memory) {
        if (!block.free && block.processId != -1) {
            seen.insert(block.processId);
        }
    }
   std::cout << "[COUNT] Unique processes in memory: " << seen.size() << "\n";
    return static_cast<int>(seen.size());*/
    
}

int MemoryManager::calculateExternalFragmentation() const {
    // CHANGE: Calculate fragmentation based on free frames
    int freeFrames = 0;
    for (const auto& frame : physicalFrames) {
        if (!frame.occupied) freeFrames++;
    }
    return freeFrames * memPerFrame;

    /*
    // Original implementation (commented out)
    int external = 0;
    for (const auto& block : memory) {
        if (block.free) {
            external += block.size;
        }
    }
    //  std::cout << "[FRAGMENT] Total external fragmentation (for mem size "
    //        << memPerProc << "): " << external << " bytes\n";
    return external;
    */
}

void MemoryManager::snapshot(int quantumCycle) {
    // CHANGE: Enhanced snapshot with demand paging information
    std::ostringstream filename;
    filename << "memory_stamp_" << std::setfill('0') << std::setw(2) << quantumCycle << ".txt";
    std::ofstream file(filename.str());

    // Timestamp
    std::time_t now = std::time(nullptr);
    std::tm tm_now{};
    char buf[100];
#ifdef _WIN32
    localtime_s(&tm_now, &now);
#else
    localtime_r(&now, &tm_now);
#endif
    std::strftime(buf, sizeof(buf), "(%m/%d/%Y %I:%M:%S%p)", &tm_now);

    file << "Timestamp: " << buf << "\n";
    file << "Number of processes in memory: " << processMemoryMap.size() << "\n";
    file << "Total external fragmentation in bytes: " << calculateExternalFragmentation() << "\n";

    // CHANGE: Add paging statistics
    int framesInUse = 0;
    for (const auto& frame : physicalFrames) {
        if (frame.occupied) framesInUse++;
    }

    file << "Pages in memory: " << framesInUse << " / " << physicalFrames.size() << "\n";
    file << "Pages swapped out: " << numPagedOut << "\n\n";

    file << "----end---- = " << maxOverallMem << "\n";

    // CHANGE: Show frame-based memory layout
    for (int i = physicalFrames.size() - 1; i >= 0; --i) {
        const Frame& frame = physicalFrames[i];
        if (frame.occupied) {
            int upper = (i + 1) * memPerFrame;
            int lower = i * memPerFrame;
            file << upper << "\n";
            file << "P" << frame.processId << " (Page " << frame.pageNumber << ")\n";
            file << lower << "\n\n";
        }
    }

    file << "----start---- = 0\n";

    /*
    // Original snapshot implementation (commented out)
    file << "----end---- = " << totalMemory << "\n";

    // Print blocks in reverse order
    for (auto it = memory.rbegin(); it != memory.rend(); ++it) {
        const auto& block = *it;
        if (!block.free && block.processId != -1 && block.size > 0) {
            int upper = block.start + block.size;
            file << upper << "\n";
            file << "P" << block.processId << "\n";
            file << block.start << "\n\n";
        }
    }

    file << "----start---- = 0\n";
    //  std::cout << "[SNAPSHOT] Snapshot written to " << filename.str() << "\n";
    */
}

void MemoryManager::printReport() const {
    std::cout << "\n========================================\n";
    std::cout << "         MEMORY MANAGER REPORT          \n";
    std::cout << "========================================\n";

    // Memory Statistics
    std::cout << "\nMEMORY STATISTICS:\n";
    std::cout << "------------------\n";
    std::cout << "Total Memory:     " << maxOverallMem << " bytes\n";
    std::cout << "Used Memory:      " << getUsedMemory() << " bytes\n";
    std::cout << "Free Memory:      " << getFreeMemory() << " bytes\n";
    std::cout << "Memory per Frame: " << memPerFrame << " bytes\n";
    std::cout << "Total Frames:     " << physicalFrames.size() << "\n";

    // Calculate frames in use
    int framesInUse = 0;
    for (const auto& frame : physicalFrames) {
        if (frame.occupied) framesInUse++;
    }
    std::cout << "Frames in Use:    " << framesInUse << "\n";
    std::cout << "Free Frames:      " << (physicalFrames.size() - framesInUse) << "\n";

    // Memory utilization percentage
    double memUtilization = (double)getUsedMemory() / maxOverallMem * 100.0;
    std::cout << "Memory Utilization: " << std::fixed << std::setprecision(2)
        << memUtilization << "%\n";

    // Paging Statistics
    std::cout << "\nPAGING STATISTICS:\n";
    std::cout << "------------------\n";
    std::cout << "Pages In:         " << numPagedIn << "\n";
    std::cout << "Pages Out:        " << numPagedOut << "\n";
    std::cout << "Total Page Faults: " << (numPagedIn + numPagedOut) << "\n";

    // CPU Statistics
    std::cout << "\nCPU STATISTICS:\n";
    std::cout << "---------------\n";
    std::cout << "Idle cpu ticks:   " << getTotalIdleTicks() << "\n";
    std::cout << "Active cpu ticks: " << getTotalActiveTicks() << "\n";
    std::cout << "Total cpu ticks:  " << getTotalTicks() << "\n";
    std::cout << "Current tick:     " << getCurrentTick() << "\n";

    // IMPROVED: Add CPU utilization percentage
    int totalTicks = getTotalTicks();
    if (totalTicks > 0) {
        double utilization = (double)getTotalActiveTicks() / totalTicks * 100.0;
        std::cout << "CPU utilization:  " << std::fixed << std::setprecision(2)
            << utilization << "%\n";
    }


    // Process Information
    std::cout << "\nPROCESS INFORMATION:\n";
    std::cout << "--------------------\n";
    std::cout << "Active Processes: " << processMemoryMap.size() << "\n";

    if (!processMemoryMap.empty()) {
        std::cout << "\nDetailed Process Memory Usage:\n";
        for (const auto& pair : processMemoryMap) {
            const ProcessMemory& procMem = pair.second;

            // Count pages in memory
            int pagesInMemory = 0;
            for (const auto& page : procMem.pages) {
                if (page.inMemory) pagesInMemory++;
            }

            std::cout << "  Process " << procMem.processId << ":\n";
            std::cout << "    Allocated Memory: " << procMem.allocatedMemory << " bytes\n";
            std::cout << "    Total Pages:      " << procMem.pages.size() << "\n";
            std::cout << "    Pages in Memory:  " << pagesInMemory << "\n";
            std::cout << "    Pages in Store:   " << (procMem.pages.size() - pagesInMemory) << "\n";
            std::cout << "    Variables:        " << procMem.symbolTable.size() << " / 32\n";

            if (procMem.hasMemoryViolation) {
                std::cout << "    ** MEMORY VIOLATION at " << procMem.violationTime
                    << " - Address " << procMem.violationAddress << " **\n";
            }
        }
    }

    // External Fragmentation
    std::cout << "\nFRAGMENTATION:\n";
    std::cout << "--------------\n";
    std::cout << "External Fragmentation: " << calculateExternalFragmentation() << " bytes\n";

    // System Status
    std::cout << "\nSYSTEM STATUS:\n";
    std::cout << "--------------\n";
    std::cout << "Global Time:      " << globalTime << "\n";
    std::cout << "Backing Store:    " << (backingStore.is_open() ? "Active" : "Inactive") << "\n";

    std::cout << "\n========================================\n\n";
}

bool MemoryManager::isProcessAllocated(int processId) const {
    // THIS CHECKS IF THE PROCESS EXISTS IN THE MAP:
    return processMemoryMap.find(processId) != processMemoryMap.end();
}

void MemoryManager::finish() {
    _shouldStop.store(true);
    // Add logic here to wait for any worker threads to finish.
    // For example, if you have a thread for dummy memory access, you would join it here.
}
