#include "stack_frame.hpp"

#include <iostream>
#include <sstream>

using namespace std;

namespace pathfinder
{

bool stack_frame::operator==(const stack_frame &other) const {
    return
        function == other.function &&
        file == other.file &&
        line == other.line &&
        binary_address == other.binary_address;
}

bool stack_frame::operator!=(const stack_frame &other) const {
    return !(*this == other);
}

bool stack_frame::operator<(const stack_frame &other) const {
    return binary_address < other.binary_address;
}

string stack_frame::str(void) const {
    stringstream ss;
    ios::fmtflags f(ss.flags());
    // This needs to be uppercase
    ss << "0x" << std::hex << std::uppercase << binary_address << ": ";
    ss.flags(f);
    ss << function << " (" << file << ":" << line << ")";
    ss.flush();
    return ss.str();
}

}  // namespace pathfinder