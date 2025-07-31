#include "MemoryManager.h"
#include <fstream>
#include <sstream>
#include <iomanip>
#include <ctime>
#include <set>
#include <iostream> // For debugging output

MemoryManager memoryManager(16384, 4096, 16); // default config

MemoryManager::MemoryManager(int total, int perProc, int frameSize)
    : totalMemory(total), memPerProc(perProc), frameSize(frameSize), nextSnapshot(0)
{
    memory.push_back({ 0, totalMemory, -1, true });
    std::cout << "[INIT] Memory initialized with block: start=0, size=" << totalMemory << "\n";
}

bool MemoryManager::allocate(int processId) {
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
}

void MemoryManager::release(int processId) {
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
}

int MemoryManager::getProcessCount() const {
    std::set<int> seen;
    for (const auto& block : memory) {
        if (!block.free && block.processId != -1) {
            seen.insert(block.processId);
        }
    }
 //   std::cout << "[COUNT] Unique processes in memory: " << seen.size() << "\n";
    return static_cast<int>(seen.size());
}

int MemoryManager::calculateExternalFragmentation() const {
    int external = 0;
    for (const auto& block : memory) {
        if (block.free) {
            external += block.size;
        }
    }
  //  std::cout << "[FRAGMENT] Total external fragmentation (for mem size "
   //     << memPerProc << "): " << external << " bytes\n";
    return external;
}


void MemoryManager::snapshot(int quantumCycle) {
    std::ostringstream filename;
    filename << "memory_stamp_" << std::setfill('0') << std::setw(2) << quantumCycle << ".txt";
    std::ofstream file(filename.str());

    // Timestamp
    std::time_t now = std::time(nullptr);
    std::tm tm_now{};
    char buf[100];
    localtime_s(&tm_now, &now);

    std::strftime(buf, sizeof(buf), "(%m/%d/%Y %I:%M:%S%p)", &tm_now);

    file << "Timestamp: " << buf << "\n";
    file << "Number of processes in memory: " << getProcessCount() << "\n";
    file << "Total external fragmentation in KB: " << (calculateExternalFragmentation()) << "\n\n";

    file << "----end---- = " << totalMemory << "\n";

    // Print blocks in reverse order
    for (auto it = memory.rbegin(); it != memory.rend(); ++it) {
        const auto& block = *it;
        if (!block.free && block.processId != -1 && block.size > 0) {
            int upper = block.start + block.size;
            file << upper << "\n";
            file << "P" << block.processId << "\n";
            file << block.start << "\n";
        }
    }

    file << "----start---- = 0\n";
  //  std::cout << "[SNAPSHOT] Snapshot written to " << filename.str() << "\n";
}
