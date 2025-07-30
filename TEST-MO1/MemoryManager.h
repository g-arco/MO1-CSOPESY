// MemoryManager.h
#pragma once

#include "Screen.h"
#include <vector>
#include <memory>

struct MemoryBlock {
    int start_addr;
    int pid;
};

class MemoryManager {
public:
    MemoryManager(int totalMem, int memPerProc);

    bool insertProcess(const std::shared_ptr<Screen>& proc);
    void removeFinished(const std::vector<std::shared_ptr<Screen>>& screens);
    int getExternalFragmentation() const;
    void generateMemoryStamp(int cycle) const;

private:
    int findFirstFit() const;

    int totalMemory;
    int memPerProc;
    std::vector<MemoryBlock> allocated;
};
