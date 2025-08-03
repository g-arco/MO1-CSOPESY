#include "MemoryManager.h"
#include <fstream>
#include <sstream>
#include <iomanip>
#include <ctime>
#include <set>
#include <iostream>
#include <algorithm>
#include <cstring>

// Original global declaration (commented out)
// MemoryManager memoryManager(16384, 4096, 16); // default config

// CHANGE: New constructor for demand paging
MemoryManager::MemoryManager(int maxMem, int minProc, int maxProc, int frameSize)
    : maxOverallMem(maxMem), minMemPerProc(minProc), maxMemPerProc(maxProc),
    memPerFrame(frameSize), globalTime(0), numPagedIn(0), numPagedOut(0),
    idleCpuTicks(0), activeCpuTicks(0), totalCpuTicks(0),
    // Original members (kept for compatibility)
    totalMemory(maxMem), memPerProc(minProc), frameSize(frameSize), nextSnapshot(0)
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
    memory.push_back({ 0, totalMemory, -1, true });
    std::cout << "[INIT] Memory initialized with " << numFrames << " frames of " << memPerFrame << " bytes each\n";
    std::cout << "[INIT] Total memory: " << maxOverallMem << " bytes\n";
}

MemoryManager::~MemoryManager() {
    if (backingStore.is_open()) {
        backingStore.close();
    }
}

// CHANGE: New method to allocate process with specific memory size
bool MemoryManager::allocateProcess(int processId, int memorySize) {
    std::lock_guard<std::mutex> lock(memoryMutex);

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
    int numPages = (memorySize + memPerFrame - 1) / memPerFrame;

    // Initialize pages (not in memory initially - demand paging)
    for (int i = 0; i < numPages; ++i) {
        Page page;
        page.pageNumber = i;
        page.inMemory = false;
        page.frameNumber = -1;
        procMem.pages.push_back(page);
    }

    processMemoryMap[processId] = procMem;
   // std::cout << "[MEMORY] Allocated " << memorySize << " bytes (" << numPages << " pages) for process " << processId << "\n";
    return true;
}

// CHANGE: New method to deallocate process memory
void MemoryManager::deallocateProcess(int processId) {
    std::lock_guard<std::mutex> lock(memoryMutex);

    auto it = processMemoryMap.find(processId);
    if (it == processMemoryMap.end()) {
        return;  // Process not found, return early
    }

    ProcessMemory& procMem = it->second;

    // Free all frames used by this process
    for (auto& page : procMem.pages) {
        if (page.inMemory && page.frameNumber != -1) {
            physicalFrames[page.frameNumber].occupied = false;
            physicalFrames[page.frameNumber].processId = -1;
            physicalFrames[page.frameNumber].pageNumber = -1;
        }
    }

    processMemoryMap.erase(it);
   // std::cout << "[MEMORY] Deallocated memory for process " << processId << "\n";
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

    for (int i = 1; i < physicalFrames.size(); ++i) {
        if (physicalFrames[i].occupied) {
            int frameTime = processMemoryMap[physicalFrames[i].processId].pages[physicalFrames[i].pageNumber].lastAccessed;
            if (frameTime < oldestTime) {
                oldestTime = frameTime;
                victimFrame = i;
            }
        }
    }

    return victimFrame;
}

// CHANGE: Page in operation
void MemoryManager::pageIn(int processId, int pageNumber) {
    int frameNumber = findFreeFrame();

    if (frameNumber == -1) {
        // No free frame, need to page out
        frameNumber = selectVictimFrame();
        pageOut(frameNumber);
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
    if (it != processMemoryMap.end()) {
        it->second.pages[pageNumber].inMemory = true;
        it->second.pages[pageNumber].frameNumber = frameNumber;
        it->second.pages[pageNumber].lastAccessed = ++globalTime;
    }

    numPagedIn++;
    //std::cout << "[PAGING] Paged in: Process " << processId << ", Page " << pageNumber << " -> Frame " << frameNumber << "\n";
}

// CHANGE: Page out operation
void MemoryManager::pageOut(int frameNumber) {
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
  //  std::cout << "[READ] PID " << processId << " reading from 0x"
    //    << std::hex << address << std::dec << "\n";

    std::lock_guard<std::mutex> lock(memoryMutex);  // Add thread safety

    auto it = processMemoryMap.find(processId);
    if (it == processMemoryMap.end()) {
        // std::cerr << "[READ] ERROR: Process " << processId << " not found in memory manager.\n";
        // throw std::runtime_error("Process not found in memory manager");
        // CRITICAL: Don't continue execution after this point!
    }

    ProcessMemory& procMem = it->second;  // Safe to access now

    if (address >= it->second.allocatedMemory) {
       // std::cerr << "[READ] MEMORY VIOLATION: PID " << processId
         //   << " tried to read 0x" << std::hex << address << std::dec << "\n";

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

    if (!procMem.pages[pageNumber].inMemory) {
        //std::cout << "[PAGE FAULT] PID " << processId << " reading page " << pageNumber
       //     << " (addr 0x" << std::hex << address << std::dec << ")\n";
        pageIn(processId, pageNumber);

        if (!procMem.pages[pageNumber].inMemory) {
          //  std::cerr << "[READ] ERROR: Page " << pageNumber << " still not in memory after pageIn.\n";
           // throw std::runtime_error("Failed to bring page into memory after page fault");
        }
    }

    procMem.pages[pageNumber].lastAccessed = ++globalTime;
    int frameNumber = procMem.pages[pageNumber].frameNumber;
    Frame& frame = physicalFrames[frameNumber];

    uint16_t value = 0;
    if (offset + 1 < frame.data.size()) {
        value = (frame.data[offset + 1] << 8) | frame.data[offset];
    }

    //std::cout << "[READ] PID " << processId << " value at 0x"
      //  << std::hex << address << ": " << value << std::dec << "\n";

    return value;
}



void MemoryManager::writeMemory(int processId, uint32_t address, uint16_t value) {
   // std::cout << "[WRITE] PID " << processId << " writing " << value << " to 0x"
     //   << std::hex << address << std::dec << "\n";

    std::lock_guard<std::mutex> lock(memoryMutex);  // Add thread safety

    auto it = processMemoryMap.find(processId);
    if (it == processMemoryMap.end()) {
        // std::cerr << "[WRITE] ERROR: Process " << processId << " not found in memory manager.\n";
        //throw std::runtime_error("Process not found in memory manager");
        // CRITICAL: Don't continue execution after this point!
    }

    ProcessMemory& procMem = it->second;  // Safe to access now


    if (address >= it->second.allocatedMemory) {
      //  std::cerr << "[WRITE] MEMORY VIOLATION: PID " << processId
        //    << " tried to write 0x" << std::hex << address << std::dec << "\n";

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

    if (!procMem.pages[pageNumber].inMemory) {
        //std::cout << "[PAGE FAULT] PID " << processId << " writing page " << pageNumber
          //  << " (addr 0x" << std::hex << address << std::dec << ")\n";
        pageIn(processId, pageNumber);

        if (!procMem.pages[pageNumber].inMemory) {
         //   std::cerr << "[WRITE] ERROR: Page " << pageNumber << " still not in memory after pageIn.\n";
           // throw std::runtime_error("Failed to bring page into memory after page fault");
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
bool MemoryManager::declareVariable(int processId, const std::string& varName, uint16_t value) {
    std::lock_guard<std::mutex> lock(memoryMutex);

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
    std::lock_guard<std::mutex> lock(memoryMutex);

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
    std::cout << "\n=========================================================================================\n";
    std::cout << "| PROCESS-SMI 24.6.1       Driver Version: 12.0.1    CUDA Version: N/A               |\n";
    std::cout << "=========================================================================================\n";

    int usedMemory = getUsedMemory();
    int freeMemory = getFreeMemory();

    std::cout << "| Memory Usage: " << usedMemory << " / " << maxOverallMem << " bytes";
    std::cout << std::setw(50 - std::to_string(usedMemory).length() - std::to_string(maxOverallMem).length()) << "|\n";
    std::cout << "=========================================================================================\n";
    std::cout << "| Processes:                                                                           |\n";
    std::cout << "=========================================================================================\n";
    std::cout << "| PID   Memory Usage   Process Name                                                   |\n";
    std::cout << "=========================================================================================\n";

    for (const auto& pair : processMemoryMap) {
        const ProcessMemory& procMem = pair.second;
        std::cout << "| " << std::setw(5) << procMem.processId
            << " " << std::setw(12) << procMem.allocatedMemory << " bytes"
            << "   Process" << procMem.processId;

        // Fill remaining space
        std::string processInfo = "Process" + std::to_string(procMem.processId);
        int remaining = 65 - processInfo.length();
        std::cout << std::setw(remaining) << "|\n";
    }

    std::cout << "=========================================================================================\n\n";
}

// CHANGE: Memory debugging - vmstat command
void MemoryManager::vmstat() {
    std::cout << "\nMemory Statistics:\n";
    std::cout << "====================\n";
    std::cout << "Total memory:     " << maxOverallMem << " bytes\n";
    std::cout << "Used memory:      " << getUsedMemory() << " bytes\n";
    std::cout << "Free memory:      " << getFreeMemory() << " bytes\n";
    std::cout << "\nCPU Statistics:\n";
    std::cout << "====================\n";
    std::cout << "Idle cpu ticks:   " << idleCpuTicks << "\n";
    std::cout << "Active cpu ticks: " << activeCpuTicks << "\n";
    std::cout << "Total cpu ticks:  " << totalCpuTicks << "\n";
    std::cout << "\nPaging Statistics:\n";
    std::cout << "====================\n";
    std::cout << "Num paged in:     " << numPagedIn << "\n";
    std::cout << "Num paged out:    " << numPagedOut << "\n";
    std::cout << "\nProcess Information:\n";
    std::cout << "====================\n";

    for (const auto& pair : processMemoryMap) {
        const ProcessMemory& procMem = pair.second;
        std::cout << "Process " << procMem.processId << ":\n";
        std::cout << "  Allocated: " << procMem.allocatedMemory << " bytes\n";
        std::cout << "  Pages in memory: ";

        int pagesInMem = 0;
        for (const auto& page : procMem.pages) {
            if (page.inMemory) pagesInMem++;
        }
        std::cout << pagesInMem << " / " << procMem.pages.size() << "\n";

        if (procMem.hasMemoryViolation) {
            std::cout << "  ** MEMORY VIOLATION at " << procMem.violationTime
                << " - Address " << procMem.violationAddress << " **\n";
        }
    }
    std::cout << "\n";
}

// CHANGE: Helper methods for statistics
int MemoryManager::getUsedMemory() const {
    int used = 0;
    for (const auto& pair : processMemoryMap) {
        used += pair.second.allocatedMemory;
    }
    return used;
}

int MemoryManager::getFreeMemory() const {
    return maxOverallMem - getUsedMemory();
}

void MemoryManager::updateCpuTicks(int idle, int active) {
    idleCpuTicks += idle;
    activeCpuTicks += active;
    totalCpuTicks += (idle + active);
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

    /*
    // Original implementation (commented out)
    //std::cout << "[ALLOCATE] Attempting to allocate for P" << processId << "\n";
    for (size_t i = 0; i < memory.size(); ++i) {
        auto& block = memory[i];
      //  std::cout << "[ALLOCATE] Inspecting block: start=" << block.start << ", size=" << block.size << ", free=" << block.free << "\n";
        if (block.free && block.size >= memPerProc) {
            Block allocated = { block.start, memPerProc, processId, false };
            block.start += memPerProc;
            block.size -= memPerProc;

            if (block.size == 0) {
                memory.erase(memory.begin() + i);
        //        std::cout << "[ALLOCATE] Erased empty block after allocation.\n";
            }
            else {
                memory[i] = block;
          //      std::cout << "[ALLOCATE] Updated remaining block: start=" << block.start << ", size=" << block.size << "\n";
            }

            memory.insert(memory.begin() + i, allocated);
        //    std::cout << "[ALLOCATE] Allocated block for P" << processId << ": start=" << allocated.start << ", size=" << allocated.size << "\n";
            calculateExternalFragmentation();
            return true;
        }
    }
  //  std::cout << "[ALLOCATE] Failed to allocate for P" << processId << "\n";
    calculateExternalFragmentation();
    return false;
    */
}

void MemoryManager::release(int processId) {
    // CHANGE: Use new deallocation method
    deallocateProcess(processId);

    /*
    // Original implementation (commented out)
 //   std::cout << "[RELEASE] Releasing memory for P" << processId << "\n";
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
 //   std::cout << "[COUNT] Unique processes in memory: " << seen.size() << "\n";
    return static_cast<int>(seen.size());
    */
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
   //     << memPerProc << "): " << external << " bytes\n";
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
    file << "Number of processes in memory: " << getProcessCount() << "\n";
    file << "Total external fragmentation in bytes: " << calculateExternalFragmentation() << "\n";

    // CHANGE: Add paging statistics
    file << "Pages in memory: " << (physicalFrames.size() - calculateExternalFragmentation() / memPerFrame) << " / " << physicalFrames.size() << "\n";
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
    std::cout << "         MEMORY MANAGER REPORT         \n";
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
    std::cout << "Idle CPU Ticks:   " << idleCpuTicks << "\n";
    std::cout << "Active CPU Ticks: " << activeCpuTicks << "\n";
    std::cout << "Total CPU Ticks:  " << totalCpuTicks << "\n";

    if (totalCpuTicks > 0) {
        double cpuUtilization = (double)activeCpuTicks / totalCpuTicks * 100.0;
        std::cout << "CPU Utilization:  " << std::fixed << std::setprecision(2)
            << cpuUtilization << "%\n";
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