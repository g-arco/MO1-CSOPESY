#pragma once
#ifndef MEMORY_MANAGER_H
#define MEMORY_MANAGER_H

#include <vector>
#include <map>
#include <string>

class MemoryManager {
private:
    struct Block {
        int start;
        int size;
        int processId;
        bool free;
    };

    int totalMemory;
    int memPerProc;
    int frameSize;
    std::vector<Block> memory;
    int nextSnapshot;

public:
    MemoryManager(int total, int perProc, int frameSize);

    bool allocate(int processId);
    void release(int processId);
    void snapshot(int quantumCycle);

    int getProcessCount() const;
    int calculateExternalFragmentation() const;
};

extern MemoryManager memoryManager;

#endif