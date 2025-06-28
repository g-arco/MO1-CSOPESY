#ifndef INSTRUCTION_H
#define INSTRUCTION_H

#include <string>
#include <vector>

enum class InstructionType { PRINT, DECLARE, ADD, SUBTRACT, SLEEP, FOR };

struct Instruction {
    InstructionType type;
    std::vector<std::string> args;
};

#endif
