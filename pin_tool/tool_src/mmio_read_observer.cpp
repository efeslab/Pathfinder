#include "pin.H"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/syscall.h>
#include <fstream>
#include <iostream>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

KNOB< std::string > if_knob(
    KNOB_MODE_WRITEONCE, "pintool", "i", "", "Input file of addresses");
KNOB< std::string > of_knob(
    KNOB_MODE_WRITEONCE, "pintool", "o", "", "Output file of addresses");

class store_id{
public:
    ADDRINT addr;
    size_t size;
    size_t reads{0};
    size_t writes{0};
    store_id() {}
    store_id(ADDRINT _addr, size_t _size) : addr{_addr}, size{_size} {}
};

std::set<size_t> id_set;
std::unordered_map<size_t, store_id> id_mapping;

bool overlap(size_t A_start, size_t A_size, size_t B_start, size_t B_size) {
    size_t A_end = A_start + A_size;
    size_t B_end = B_start + B_size;
    return ((A_start < B_end) && (B_start < A_end));
}

VOID AddressRead(ADDRINT addr, size_t sz) {
    for (size_t id : id_set) {
        store_id& store = id_mapping[id];
        if (overlap(static_cast<size_t>(store.addr), store.size, static_cast<size_t>(addr), sz)) {
            store.reads++;
        }
    }
}

VOID AddressWrite(ADDRINT addr, size_t sz) {
    for (size_t id : id_set) {
        store_id& store = id_mapping[id];
        if (overlap(static_cast<size_t>(store.addr), store.size, static_cast<size_t>(addr), sz)) {
            store.writes++;
        }
    }
}

VOID InstrumentTrace(TRACE trace, VOID *v) {
    for (BBL bbl = TRACE_BblHead(trace); BBL_Valid(bbl); bbl = BBL_Next(bbl)) {
        for (INS ins = BBL_InsHead(bbl); INS_Valid(ins); ins = INS_Next(ins)) {
            size_t memOperands = INS_MemoryOperandCount(ins);
            for (size_t memOp = 0; memOp < memOperands; memOp++) {

                if (INS_MemoryOperandIsRead(ins, memOp)) {
                    // If the instruction is a memory read within memory-mapped space
                    INS_InsertCall(ins, IPOINT_BEFORE, (AFUNPTR)AddressRead,
                                    IARG_MEMORYREAD_EA, // Address of memory read
                                    IARG_MEMORYREAD_SIZE, // Size of memory read
                                    IARG_END);
                }
                if (INS_MemoryOperandIsWritten(ins, memOp)) {
                    // If the instruction is a memory write within memory-mapped space
                    INS_InsertCall(ins, IPOINT_BEFORE, (AFUNPTR)AddressWrite,
                                    IARG_MEMORYWRITE_EA, // Address of memory write
                                    IARG_MEMORYWRITE_SIZE, // Size of memory write
                                    IARG_END);
                }
            }
        }
    }
}

VOID Fini(INT32 code, VOID* v) {

    std::string *output_file = (std::string*)v;

    std::ofstream outfile(output_file->c_str());
    if (!outfile.is_open()) {
        std::cerr << "Could not open input file: " << output_file << std::endl;
        exit(EXIT_FAILURE);
    }

    std::cout << std::endl << "[PINTOOL STDOUT BEGIN]" << std::endl;
    for (size_t id : id_set) {
        store_id& store = id_mapping[id];
        std::cout << "Store ID: " << id << ", reads: " << store.reads << ", (over)writes: " << store.writes << "\n";
        outfile << "Store ID: " << id << ", reads: " << store.reads << ", (over)writes: " << store.writes << "\n";
    }
    std::cout << std::endl << "[PINTOOL STDOUT END]" << std::endl;

    outfile.close();
    delete output_file;
}

void parse_input_file(std::ifstream &infile) {
    std::cout << "[PINTOOL STDOUT BEGIN]" << std::endl;

    std::string line;
    while (getline(infile, line)) {
        size_t id, size;
        ADDRINT address;
        // parse the line to get the data fields
        sscanf(line.c_str(), "%ld, %ld, %ld", &id, &address, &size);
        // print out the data fields
        std::cout << "id: " << id << ", address: " << address << ", size: " << size << std::endl;
        store_id store(address, size);
        id_mapping[id] = store;
        id_set.insert(id);
    }

    std::cout << "[PINTOOL STDOUT END]" << std::endl << std::endl;
}

int main(int argc, char *argv[]) {
    PIN_InitSymbols();
    if (PIN_Init(argc, argv) != 0) {
        std::cerr << "Usage: " << argv[0] << " -i <input_file> -o <output_file>"
            << std::endl;
        return 1;
    }

    std::string input_file = if_knob.Value();
    if (input_file.empty()) {
        std::cerr << "No input file specified" << std::endl;
        return 1;
    }

    std::ifstream infile(input_file.c_str());
    if (!infile.is_open()) {
        std::cerr << "Could not open input file: " << input_file << std::endl;
        return 1;
    }
    parse_input_file(infile);
    infile.close();

    std::string *output_file = new std::string(of_knob.Value());
    if (output_file->empty()) {
        std::cerr << "No output file specified" << std::endl;
        return 1;
    }

    TRACE_AddInstrumentFunction(InstrumentTrace, 0);
    PIN_AddFiniFunction(Fini, (VOID*)output_file);
    PIN_StartProgram();

    return 0;
}






// PIN_AddSyscallEntryFunction(SyscallEntry, 0);
// PIN_AddSyscallExitFunction(SyscallExit, 0);

/*
// Data structure to store information about an ongoing mmap system call
struct SyscallInfo {
    ADDRINT addr;
    ADDRINT length;
    ADDRINT syscallNumber;
    int fd;

    SyscallInfo() : addr(0), length(0), syscallNumber(0), fd(0) {}
};

// Map to store ongoing mmap system calls, keyed by thread ID
std::unordered_map<THREADID, SyscallInfo> ongoingMmapCalls;
*/

/*
VOID SyscallEntry(THREADID tid, CONTEXT *ctxt, SYSCALL_STANDARD std, VOID *v)
{
    int syscallNumber = PIN_GetSyscallNumber(ctxt, std);
    SyscallInfo &callInfo = ongoingMmapCalls[tid];
    callInfo.syscallNumber = syscallNumber;

	if (syscallNumber == __NR_mmap) {
        callInfo.length = PIN_GetSyscallArgument(ctxt, std, 1); // length
        callInfo.fd = (int) PIN_GetSyscallArgument(ctxt, std, 4); // fd
        // get the path 
	}
    else if (callInfo.syscallNumber == __NR_munmap) {
        callInfo.addr = PIN_GetSyscallArgument(ctxt, std, 0); // addr
        callInfo.length = PIN_GetSyscallArgument(ctxt, std, 1); // length
    }
}

VOID SyscallExit(THREADID tid, CONTEXT *ctxt, SYSCALL_STANDARD std, VOID *v)
{
    SyscallInfo &callInfo = ongoingMmapCalls[tid];

	if (callInfo.syscallNumber == __NR_mmap && callInfo.fd != -1) {
        callInfo.addr = PIN_GetSyscallReturn(ctxt, std);

        std::cout << "id: " << num_ids << ", address: " << callInfo.addr << ", size: " << callInfo.length << std::endl;

        store_id mmap_range(callInfo.addr, (size_t) callInfo.length);
        id_mapping[num_ids] = mmap_range;
        num_ids++;
    }
    else if (callInfo.syscallNumber == __NR_munmap) {
        // not sure how im gonna do this one yet lol.
    }
    ongoingMmapCalls.erase(tid);
}
*/