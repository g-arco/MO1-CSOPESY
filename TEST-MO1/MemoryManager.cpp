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

void MemoryManager::initializeBackingStore() {
    std::lock_guard<std::mutex> storeLock(backingStoreMutex);

    // Create/overwrite the backing store file with a proper header
    std::ofstream backingStoreFile("csopesy-backing-store.txt", std::ios::out | std::ios::trunc);
    if (!backingStoreFile.is_open()) {
        std::cerr << "[ERROR] Failed to create backing store file.\n";
        return;
    }

    // Get current timestamp
    auto now = std::time(nullptr);
    std::tm tm_now{};
    char timeBuffer[100];
#ifdef _WIN32
    localtime_s(&tm_now, &now);
#else
    localtime_r(&now, &tm_now);
#endif
    std::strftime(timeBuffer, sizeof(timeBuffer), "%Y-%m-%d %H:%M:%S", &tm_now);

    // Write file header
    backingStoreFile << "=================================================\n";
    backingStoreFile << "           CSOPESY BACKING STORE\n";
    backingStoreFile << "=================================================\n";
    backingStoreFile << "Initialized: " << timeBuffer << "\n";
    backingStoreFile << "Total Memory: " << maxOverallMem << " bytes\n";
    backingStoreFile << "Frame Size: " << memPerFrame << " bytes\n";
    backingStoreFile << "Total Frames: " << (maxOverallMem / memPerFrame) << "\n";
    backingStoreFile << "=================================================\n";
    backingStoreFile << "\nThis file contains pages that have been swapped\n";
    backingStoreFile << "out of physical memory due to memory pressure.\n";
    backingStoreFile << "Each page entry shows the process ID, page number,\n";
    backingStoreFile << "timestamp, and the actual memory contents in both\n";
    backingStoreFile << "hexadecimal and decimal formats.\n\n";

    backingStoreFile.close();

    //std::cout << "[INIT] Backing store initialized: csopesy-backing-store.txt\n";
}


// CRITICAL FIX: Enhanced memory allocation check for highly constrained memory
bool MemoryManager::allocateProcess(int processId, int memorySize) {
    std::lock_guard<std::recursive_mutex> lock(memoryMutex);

    int numPages = (memorySize + memPerFrame - 1) / memPerFrame;

    if (memorySize > maxOverallMem) {
        return false;
    }


    if (processMemoryMap.find(processId) != processMemoryMap.end()) {
        return false; // Already allocated
    }

    // CRITICAL: Don't check total memory usage - just allocate the process
    // Let page replacement handle the memory pressure when pages are actually accessed

   // std::cout << "[MEMORY] Allocating process " << processId
     //  << " (" << memorySize << " bytes) - relying on page replacement for memory management" << std::endl;

    // Validate memory size
    if (memorySize < 64 || memorySize > 65536) {
        return false;
    }

    if ((memorySize & (memorySize - 1)) != 0) {
        return false;
    }

    ProcessMemory procMem;
    procMem.processId = processId;
    procMem.allocatedMemory = memorySize;

    if (numPages == 0) numPages = 1;

    procMem.pages.clear();
    procMem.pages.resize(numPages);

    for (int i = 0; i < numPages; i++) {
        procMem.pages[i].frameNumber = -1;  // Not in memory initially
        procMem.pages[i].inMemory = false;  // Will be loaded on demand
        procMem.pages[i].dirty = false;
        procMem.pages[i].lastAccessed = 0;
    }

    procMem.hasMemoryViolation = false;
    procMem.violationTime = "";

    processMemoryMap[processId] = procMem;
    initializeBackingStore();

    //  std::cout << "[MEMORY] Process " << processId << " allocated with "
      // << numPages << " pages (none loaded yet)" << std::endl;
    return true;
}

// NEW: Try to free memory for new allocation
bool MemoryManager::tryFreeMemoryForAllocation(int neededMemory) {
    std::lock_guard<std::recursive_mutex> lock(memoryMutex);

    // Calculate how much memory we need to free
    int currentUsed = getUsedMemory();
    int availableMemory = maxOverallMem - currentUsed;

    if (availableMemory >= neededMemory) {
        return true; // Already have enough memory
    }

    int memoryToFree = neededMemory - availableMemory;
    std::cout << "[MEMORY] Need to free " << memoryToFree << " bytes" << std::endl;

    // Strategy: Deallocate entire processes to make room
    std::vector<int> processesToDeallocate;
    int freedMemory = 0;

    for (const auto& pair : processMemoryMap) {
        if (freedMemory >= memoryToFree) break;

        int processId = pair.first;
        int processMemory = pair.second.allocatedMemory;

        processesToDeallocate.push_back(processId);
        freedMemory += processMemory;

        std::cout << "[MEMORY] Will deallocate process " << processId
            << " (" << processMemory << " bytes)" << std::endl;
    }

    // Actually deallocate the processes
    for (int processId : processesToDeallocate) {
        deallocateProcess(processId);
    }

    return freedMemory >= memoryToFree;
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

    // std::cout << "[DEBUG] No free frames available - checking if any process can fit in memory" << std::endl;

    bool anyProcessCanFit = false;
    for (const auto& pair : processMemoryMap) {
        int processId = pair.first;
        const ProcessMemory& procMem = pair.second;
        int totalPagesNeeded = procMem.pages.size();
        int totalFramesAvailable = physicalFrames.size();

        if (minMemPerProc > maxOverallMem) { return -2;  }

       // std::cout << "[DEBUG] Process " << processId << " needs " << totalPagesNeeded
         //   << " pages, system has " << totalFramesAvailable << " frames" << std::endl;

        if (totalPagesNeeded <= totalFramesAvailable) {
            anyProcessCanFit = true;
            //std::cout << "[DEBUG] Process " << processId << " CAN fit in memory" << std::endl;
            break;
        }
        else {
          //  std::cout << "[DEBUG] Process " << processId << " CANNOT fit in memory (needs "
            //    << (totalPagesNeeded - totalFramesAvailable) << " more frames)" << std::endl;
        }
    }

    if (!anyProcessCanFit) {
       // std::cout << "[DEADLOCK] NO PROCESS CAN FIT IN MEMORY - SYSTEM DEADLOCKED!" << std::endl;
        //std::cout << "[DEADLOCK] All processes require more frames than the system has available" << std::endl;
        //std::cout << "[DEADLOCK] Page replacement cannot help - returning -1 to indicate deadlock" << std::endl;

        // Return -1 to indicate that no frame can be made available
        // This will cause the calling pageIn() to handle the deadlock condition
        return -1;
    }

    // First pass: look for truly free frames
    for (int i = 0; i < physicalFrames.size(); ++i) {
        if (!physicalFrames[i].occupied) {
            //std::cout << "[DEBUG] Found free frame: " << i << std::endl;
            return i;
        }
    }

    // If we get here, at least one process can fit, so normal page replacement should work
    //std::cout << "[DEBUG] At least one process can fit - proceeding with normal page replacement" << std::endl;
    return -1;
}

// CHANGE: Page in operation
void MemoryManager::pageIn(int processId, int pageNumber) {
    std::lock_guard<std::recursive_mutex> lock(memoryMutex);

   // std::cout << "[PAGING] Attempting to page in Process " << processId
     //   << ", Page " << pageNumber << std::endl;

    int frameNumber = findFreeFrame();

    if (frameNumber == -2) { return; };

    // CRITICAL: Force replacement when all frames are occupied
    if (frameNumber == -1) {
        // Check if this is a deadlock situation or normal "frames full" situation
        // We can distinguish by checking if current process can fit
        auto it = processMemoryMap.find(processId);
        if (it != processMemoryMap.end()) {
            int totalPagesNeeded = it->second.pages.size();
            int totalFramesAvailable = physicalFrames.size();

            if (totalPagesNeeded > totalFramesAvailable) {
                // DEADLOCK: Process cannot fit in memory
              //  std::cout << "[DEADLOCK] Process " << processId << " page " << pageNumber
                //    << " cannot be loaded - process needs " << totalPagesNeeded
                  //  << " pages but system only has " << totalFramesAvailable
                    //<< " frames total!" << std::endl;

                // DON'T update the page table - leave page as "not in memory"
                // This will cause the process to hang when it tries to access this page
                // The process will be stuck in an infinite loop trying to access this memory

                return; // Exit without loading the page - this creates the deadlock
            }
        }

        // Normal case: frames are full but process can fit with page replacement
        //std::cout << "[PAGING] All frames occupied - initiating LRU page replacement" << std::endl;

        frameNumber = selectVictimFrame();

        if (frameNumber < 0 || frameNumber >= physicalFrames.size()) {
            throw std::runtime_error("selectVictimFrame returned invalid frame: " + std::to_string(frameNumber));
        }

       //std::cout << "[PAGING] Selected victim frame " << frameNumber
         // << " (Process " << physicalFrames[frameNumber].processId
           //<< ", Page " << physicalFrames[frameNumber].pageNumber << ")" << std::endl;

        // Page out the victim
        pageOut(frameNumber);

        // Verify frame is now available
        if (physicalFrames[frameNumber].occupied) {
            throw std::runtime_error("Frame " + std::to_string(frameNumber) + " still occupied after page out!");
        }
    }

    if (frameNumber < 0 || frameNumber >= physicalFrames.size()) {
        throw std::runtime_error("Invalid frame number for page in: " + std::to_string(frameNumber));
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

    // Count occupied frames for verification
    int occupiedFrames = 0;
    for (const auto& frame : physicalFrames) {
        if (frame.occupied) occupiedFrames++;
    }

   // std::cout << "[PAGING] Successfully paged in: Process " << processId
     //   << ", Page " << pageNumber << " -> Frame " << frameNumber
      //  << " (Total page-ins: " << numPagedIn << ")" << std::endl;
   // std::cout << "[DEBUG] Frames occupied after page in: " << occupiedFrames
     //   << "/" << physicalFrames.size() << std::endl;

    // CRITICAL: Verify we haven't exceeded frame capacity
    if (occupiedFrames > physicalFrames.size()) {
        throw std::runtime_error("Frame count exceeded physical memory capacity!");
    }
}


// ENHANCED: Better victim frame selection with debug output
int MemoryManager::selectVictimFrame() {
   // std::cout << "[DEBUG] Selecting victim frame using LRU policy..." << std::endl;

    int victimFrame = -1;
    int oldestTime = INT_MAX;

    // Find the frame with the oldest last accessed time
    for (int i = 0; i < physicalFrames.size(); ++i) {
        if (physicalFrames[i].occupied) {
            int processId = physicalFrames[i].processId;
            int pageNumber = physicalFrames[i].pageNumber;

            auto it = processMemoryMap.find(processId);
            if (it != processMemoryMap.end() && pageNumber < it->second.pages.size()) {
                int lastAccessed = it->second.pages[pageNumber].lastAccessed;
               // std::cout << "[DEBUG] Frame " << i << " (Process " << processId << ", Page " << pageNumber
                  //  << ") last accessed at time " << lastAccessed << std::endl;

                if (lastAccessed < oldestTime) {
                    oldestTime = lastAccessed;
                    victimFrame = i;
                }
            }
            else {
                // Process not found or invalid page - good candidate for eviction
                std::cout << "[DEBUG] Frame " << i << " has invalid process/page - selecting as victim" << std::endl;
                return i;
            }
        }
    }

    // If no victim found through LRU, just use first occupied frame
    if (victimFrame == -1) {
        for (int i = 0; i < physicalFrames.size(); ++i) {
            if (physicalFrames[i].occupied) {
                victimFrame = i;
                break;
            }
        }
    }

   // std::cout << "[DEBUG] Selected victim frame: " << victimFrame << std::endl;
    return victimFrame;
}

// ENHANCED: Better page out operation with clearer logging
void MemoryManager::pageOut(int frameNumber) {
    std::lock_guard<std::recursive_mutex> lock(memoryMutex);

   // std::cout << "[PAGING] Starting page out for frame " << frameNumber << std::endl;

    if (frameNumber < 0 || frameNumber >= physicalFrames.size()) {
        std::cout << "[ERROR] Invalid frame number for page out: " << frameNumber << std::endl;
        return;
    }

    Frame& frame = physicalFrames[frameNumber];
    if (!frame.occupied) {
        std::cout << "[WARNING] Attempting to page out unoccupied frame " << frameNumber << std::endl;
        return;
    }

    int processId = frame.processId;
    int pageNumber = frame.pageNumber;

  //  std::cout << "[PAGING] Evicting Process " << processId << ", Page " << pageNumber
    //    << " from Frame " << frameNumber << std::endl;

    // Find the process in the map
    auto it = processMemoryMap.find(processId);
    if (it == processMemoryMap.end()) {
        std::cout << "[WARNING] Process " << processId << " not found in memory map during page out" << std::endl;
        // Process not found, just free the frame
        frame.occupied = false;
        frame.processId = -1;
        frame.pageNumber = -1;
        return;
    }

    ProcessMemory& procMem = it->second;

    // Write to backing store if dirty (you may need to track dirty bit)
    if (pageNumber < procMem.pages.size()) {
        // For now, always write to backing store since we don't track dirty bit
        writeToBackingStore(processId, pageNumber, frame.data);
      //  std::cout << "[PAGING] Wrote page " << pageNumber << " to backing store" << std::endl;
    }

    // Update page table
    if (pageNumber < procMem.pages.size()) {
        procMem.pages[pageNumber].inMemory = false;
        procMem.pages[pageNumber].frameNumber = -1;
        // Don't reset lastAccessed - keep it for LRU policy
    }

    // Free frame
    frame.occupied = false;
    frame.processId = -1;
    frame.pageNumber = -1;
    frame.data.clear(); // Clear the data

    numPagedOut++;
  //  std::cout << "[PAGING] Successfully paged out: Process " << processId
    //    << ", Page " << pageNumber << " from Frame " << frameNumber
      //  << " (Total page-outs: " << numPagedOut << ")" << std::endl;
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
    //  << std::hex << address << std::dec << "\n";

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
     //   std::cout << "[PAGE FAULT] PID " << processId << " reading page " << pageNumber
       //   << " (addr 0x" << std::hex << address << std::dec << ")\n";
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
  // std::cout << "[WRITE] PID " << processId << " writing " << value << " to 0x"
    //   << std::hex << address << std::dec << "\n";

    auto it = processMemoryMap.find(processId);
    if (it == processMemoryMap.end()) {
       // std::cerr << "[WRITE] ERROR: Process " << processId << " not found in memory manager.\n";
       throw std::runtime_error("Process not found in memory manager");
    }

    if (address >= it->second.allocatedMemory) {
         std::cerr << "[WRITE] MEMORY VIOLATION: PID " << processId
            << " tried to write 0x" << std::hex << address << std::dec << "\n";

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
       // std::cout << "[PAGE FAULT] PID " << processId << " writing page " << pageNumber
         // << " (addr 0x" << std::hex << address << std::dec << ")\n";
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
    std::lock_guard<std::mutex> storeLock(backingStoreMutex);

    // Open backing store in text mode for human readability
    std::ofstream backingStoreFile("csopesy-backing-store.txt", std::ios::out | std::ios::app);
    if (!backingStoreFile.is_open()) {
        std::cerr << "[ERROR] Failed to open backing store file for writing.\n";
        return;
    }

    // Get current timestamp
    auto now = std::time(nullptr);
    std::tm tm_now{};
    char timeBuffer[100];
#ifdef _WIN32
    localtime_s(&tm_now, &now);
#else
    localtime_r(&now, &tm_now);
#endif
    std::strftime(timeBuffer, sizeof(timeBuffer), "%Y-%m-%d %H:%M:%S", &tm_now);

    // Write page header with clear formatting
    backingStoreFile << "=====================================\n";
    backingStoreFile << "BACKING STORE PAGE ENTRY\n";
    backingStoreFile << "=====================================\n";
    backingStoreFile << "Process ID: " << processId << "\n";
    backingStoreFile << "Page Number: " << pageNumber << "\n";
    backingStoreFile << "Timestamp: " << timeBuffer << "\n";
    backingStoreFile << "Frame Size: " << memPerFrame << " bytes\n";
    backingStoreFile << "-------------------------------------\n";
    backingStoreFile << "PAGE DATA (Hexadecimal):\n";

    // Write data in hexadecimal format with memory addresses
    for (size_t i = 0; i < data.size(); i += 16) {
        // Memory address column
        backingStoreFile << std::hex << std::uppercase << std::setfill('0')
            << std::setw(8) << (pageNumber * memPerFrame + i) << ": ";

        // Hex data (16 bytes per line)
        size_t lineEnd = std::min(i + 16, data.size());
        for (size_t j = i; j < lineEnd; ++j) {
            backingStoreFile << std::hex << std::uppercase << std::setfill('0')
                << std::setw(2) << static_cast<int>(data[j]) << " ";
        }

        // Padding for alignment if less than 16 bytes
        for (size_t j = lineEnd; j < i + 16; ++j) {
            backingStoreFile << "   ";
        }

        // ASCII representation
        backingStoreFile << "| ";
        for (size_t j = i; j < lineEnd; ++j) {
            char c = static_cast<char>(data[j]);
            backingStoreFile << (std::isprint(c) ? c : '.');
        }
        backingStoreFile << "\n";
    }

    backingStoreFile << "-------------------------------------\n";
    backingStoreFile << "PAGE DATA (Decimal Values):\n";

    // Also write data in decimal format for easier debugging
    for (size_t i = 0; i < data.size(); i += 16) {
        backingStoreFile << "Offset " << std::dec << std::setfill('0')
            << std::setw(4) << i << ": ";

        size_t lineEnd = std::min(i + 16, data.size());
        for (size_t j = i; j < lineEnd; ++j) {
            backingStoreFile << std::dec << std::setfill(' ') << std::setw(3)
                << static_cast<int>(data[j]) << " ";
        }
        backingStoreFile << "\n";
    }

    backingStoreFile << "=====================================\n\n";
    backingStoreFile.close();

   // std::cout << "[BACKING STORE] Page written: Process " << processId
     //   << ", Page " << pageNumber << " (" << data.size() << " bytes)\n";
}

std::vector<uint8_t> MemoryManager::readFromBackingStore(int processId, int pageNumber) {
    std::lock_guard<std::mutex> storeLock(backingStoreMutex);
    std::vector<uint8_t> data(memPerFrame, 0);

    std::ifstream backingStoreFile("csopesy-backing-store.txt");
    if (!backingStoreFile.is_open()) {
        std::cout << "[BACKING STORE] File not found, returning zeroed page for Process "
            << processId << ", Page " << pageNumber << "\n";
        return data;
    }

    std::string line;
    bool foundPage = false;
    bool inHexData = false;
    int currentOffset = 0;

    while (std::getline(backingStoreFile, line)) {
        // Look for page header
        if (line.find("Process ID: " + std::to_string(processId)) != std::string::npos) {
            // Found matching process, now check page number
            while (std::getline(backingStoreFile, line)) {
                if (line.find("Page Number: " + std::to_string(pageNumber)) != std::string::npos) {
                    foundPage = true;
                    break;
                }
                else if (line.find("Process ID:") != std::string::npos) {
                    // Found another process entry, stop looking
                    break;
                }
            }
        }

        if (foundPage) {
            // Look for hex data section
            if (line.find("PAGE DATA (Hexadecimal):") != std::string::npos) {
                inHexData = true;
                currentOffset = 0;
                continue;
            }

            // Stop reading when we hit the decimal section or next page
            if (line.find("PAGE DATA (Decimal Values):") != std::string::npos ||
                line.find("Process ID:") != std::string::npos) {
                break;
            }

            // Parse hex data lines
            if (inHexData && line.find(":") != std::string::npos &&
                line.find("=====") == std::string::npos &&
                line.find("-----") == std::string::npos) {

                // Find the hex data part (after the address)
                size_t colonPos = line.find(':');
                size_t pipePos = line.find('|');

                if (colonPos != std::string::npos && pipePos != std::string::npos) {
                    std::string hexPart = line.substr(colonPos + 1, pipePos - colonPos - 1);
                    std::istringstream hexStream(hexPart);
                    std::string hexByte;

                    while (hexStream >> hexByte && currentOffset < data.size()) {
                        if (hexByte.length() == 2) {
                            try {
                                data[currentOffset] = static_cast<uint8_t>(
                                    std::stoul(hexByte, nullptr, 16));
                                currentOffset++;
                            }
                            catch (const std::exception&) {
                                // Skip invalid hex values
                            }
                        }
                    }
                }
            }
        }
    }

    backingStoreFile.close();

    if (foundPage) {
        //std::cout << "[BACKING STORE] Page loaded: Process " << processId
          //  << ", Page " << pageNumber << " (" << currentOffset << " bytes read)\n";
    }
    else {
        //std::cout << "[BACKING STORE] Page not found in backing store: Process "
          //  << processId << ", Page " << pageNumber << " (returning zeroed page)\n";
    }

    return data;
}

// CHANGE: Memory debugging - process-smi command
void MemoryManager::processingSmi(double cpuUtilization) {
    std::lock_guard<std::recursive_mutex> lock(memoryMutex); // Single mutex

    std::cout << "=========================================================================================\n";
    std::cout << "PROCESS-SMI 24.6.1       Driver Version: 12.0.1    CUDA Version: N/A              \n";
    std::cout << "=========================================================================================\n";

    int usedMemory = getUsedMemory();
    std::cout << "Memory Usage: " << usedMemory << " / " << maxOverallMem << " bytes\n";
    std::cout << "Memory Utilization: " << (usedMemory/ maxOverallMem) *100 << "% \n";
    std::cout << "CPU utilization: " << cpuUtilization << "% \n";
    std::cout << "=========================================================================================\n";
    std::cout << "Processes:                                                                             \n";
    std::cout << "=========================================================================================\n";
    std::cout << "PID   Memory Usage   Process Name                                                     \n";
    std::cout << "=========================================================================================\n";

    // CHANGE 6: Safe iteration
    for (const auto& pair : processMemoryMap) {
        if (!isValidProcessMemory(pair.second)) {
            continue; // Skip invalid entries
        }

        const ProcessMemory& procMem = pair.second;
        std::string processName = "Process" + std::to_string(procMem.processId);
        std::cout << std::setw(5) << procMem.processId
            << "   " << std::setw(12) << procMem.allocatedMemory << " bytes"
            << "   " << std::left << std::setw(45) << processName << "\n";
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

void MemoryManager::printReport(double cpuUtilization) const {
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
        std::cout << "CPU utilization:  " << cpuUtilization << "%\n";
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

