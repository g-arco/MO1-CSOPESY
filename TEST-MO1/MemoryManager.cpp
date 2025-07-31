#include "MemoryManager.h"
#include <fstream>
#include <sstream>
#include <iomanip>
#include <ctime>

MemoryManager memoryManager(16384, 4096, 16); // default config

MemoryManager::MemoryManager(int total, int perProc, int frameSize)
    : totalMemory(total), memPerProc(perProc), frameSize(frameSize), nextSnapshot(0)
{
    memory.push_back({ 0, totalMemory, -1, true });
}

bool MemoryManager::allocate(int processId) {
    for (size_t i = 0; i < memory.size(); ++i) {
        auto& block = memory[i];
        if (block.free && block.size >= memPerProc) {
            Block allocated = { block.start, memPerProc, processId, false };
            block.start += memPerProc;
            block.size -= memPerProc;

            if (block.size == 0)
                block.free = false;

            memory.insert(memory.begin() + i, allocated);
            return true;
        }
    }
    return false;
}


void MemoryManager::release(int processId) {
    for (auto& block : memory) {
        if (block.processId == processId) {
            block.free = true;
            block.processId = -1;
        }
    }

    for (size_t i = 0; i < memory.size() - 1; ) {
        if (memory[i].free && memory[i + 1].free) {
            memory[i].size += memory[i + 1].size;
            memory.erase(memory.begin() + i + 1);
        }
        else {
            ++i;
        }
    }
}

int MemoryManager::getProcessCount() const {
    int count = 0;
    for (const auto& block : memory) {
        if (!block.free) ++count;
    }
    return count;
}

int MemoryManager::calculateExternalFragmentation() const {
    int external = 0;
    for (const auto& block : memory) {
        if (block.free && block.size < memPerProc) {
            external += block.size;
        }
    }
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
    file << "Total external fragmentation in KB: " << (calculateExternalFragmentation() / 1024) << "\n\n";

    file << "----end---- = " << totalMemory << "\n";

    // Print blocks in reverse (high ? low)
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
}
