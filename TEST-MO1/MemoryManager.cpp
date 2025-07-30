// MemoryManager.cpp
#include "MemoryManager.h"
#include <algorithm>
#include <fstream>
#include <iomanip>
#include <ctime>
#include <sstream>

MemoryManager::MemoryManager(int totalMem, int memPerProc)
    : totalMemory(totalMem), memPerProc(memPerProc) {
}

int MemoryManager::findFirstFit() const {
    if (allocated.empty()) return 0;
    int prev_end = 0;
    for (const auto& block : allocated) {
        int gap = block.start_addr - prev_end;
        if (gap >= memPerProc) return prev_end;
        prev_end = block.start_addr + memPerProc;
    }
    if (totalMemory - prev_end >= memPerProc)
        return prev_end;
    return -1;
}

bool MemoryManager::insertProcess(const std::shared_ptr<Screen>& proc) {
    int addr = findFirstFit();
    if (addr != -1) {
        allocated.push_back({ addr, proc->getProcessId() });
        std::sort(allocated.begin(), allocated.end(),
            [](const MemoryBlock& a, const MemoryBlock& b) {
                return a.start_addr < b.start_addr;
            });
        // Store where it was placed if you want
        // proc->setMemoryAddress(addr); // Optional: add setter if needed
        return true;
    }
    return false; // Not enough memory
}

void MemoryManager::removeFinished(const std::vector<std::shared_ptr<Screen>>& screens) {
    for (const auto& proc : screens) {
        if (proc->isFinished()) {
            allocated.erase(std::remove_if(allocated.begin(), allocated.end(),
                [&](const MemoryBlock& block) {
                    return block.pid == proc->getProcessId();
                }), allocated.end());
        }
    }
}

int MemoryManager::getExternalFragmentation() const {
    if (allocated.empty()) return totalMemory;
    int ext_frag = 0;
    int prev_end = 0;
    for (const auto& block : allocated) {
        int gap = block.start_addr - prev_end;
        if (gap > 0) ext_frag += gap;
        prev_end = block.start_addr + memPerProc;
    }
    if (totalMemory > prev_end)
        ext_frag += totalMemory - prev_end;
    return ext_frag;
}

void MemoryManager::generateMemoryStamp(int cycle) const {
    std::stringstream filename;
    filename << "memory_stamp_" << std::setw(2) << std::setfill('0') << cycle << ".txt";
    std::ofstream out(filename.str());

    std::time_t now = std::time(0);
    tm* ltm = std::localtime(&now);
    out << "Timestamp: (" << std::put_time(ltm, "%m/%d/%Y %I:%M:%S%p") << ")\n";
    out << "Number of processes in memory: " << allocated.size() << "\n";
    out << "Total external fragmentation in KB: " << getExternalFragmentation() << "\n\n";

    out << "----- end ----- = " << totalMemory << "\n";

    std::vector<MemoryBlock> sorted = allocated;
    std::sort(sorted.rbegin(), sorted.rend(),
        [](const MemoryBlock& a, const MemoryBlock& b) {
            return a.start_addr < b.start_addr;
        });

    for (const auto& block : sorted) {
        int end = block.start_addr + memPerProc;
        int start = block.start_addr;
        out << end << "\nP" << block.pid << "\n" << start << "\n\n";
    }

    out << "--- start --- = 0\n";
    out.close();
}
