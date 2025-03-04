#pragma once

#include <cstdint>
#include <string>

namespace pathfinder
{

struct stack_frame {
    std::string function;
    std::string file;
    int line;
    uint64_t binary_address;

    std::string str(void) const;

    bool operator==(const stack_frame &other) const;
    bool operator!=(const stack_frame &other) const;
    bool operator<(const stack_frame &other) const;
};


}  // namespace pathfinder