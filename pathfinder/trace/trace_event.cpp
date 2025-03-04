#include "trace_event.hpp"

#include <iostream>

using namespace std;
namespace icl = boost::icl;

namespace pathfinder
{

static const char *event_type_strs[] = {
    "STORE", "FLUSH", "FENCE",
    "ASSERT_PERSISTED", "ASSERT_ORDERED", "REQUIRED_FLUSH",
    "REGISTER_FILE", "STOP", "WRITE", "REGISTER_WRITE_FILE", "PWRITEV",
    "FTRUNCATE", "FALLOCATE",
    PATHFINDER_BEGIN_TOKEN, PATHFINDER_END_TOKEN, 
    "UNREGISTER_FILE", "MSYNC", "PWRITE64", "WRITEV", "LSEEK", "RENAME", "UNLINK", "FSYNC", "FDATASYNC", "OPEN", "CREAT", "CLOSE", "MKDIR", "RMDIR", "SYNC", "SYNCFS", "SYNC_FILE_RANGE",
    PATHFINDER_OP_BEGIN_TOKEN, PATHFINDER_OP_END_TOKEN
};

const char *event_type_to_str(event_type t) {
    return event_type_strs[t];
}

/* trace_event */

icl::discrete_interval<uint64_t> trace_event::range(void) const {
    if (type != STORE && type != FLUSH && type != MSYNC && type != REGISTER_FILE && type != UNREGISTER_FILE) {
        cerr << "invalid operation (range) on event of type " <<
            event_type_to_str(type) << "\n";
        exit(EXIT_FAILURE);
    }
    return icl::interval<uint64_t>::right_open(address, address + size);
}

icl::discrete_interval<uint64_t> trace_event::cacheline_range(void) const {
    auto erange = range();
    assert(erange.lower() < erange.upper());
    uint64_t cl_start = erange.lower() & (~63);
    uint64_t cl_end = ( (erange.upper() - 1) & (~63)) + 64;
    return icl::interval<uint64_t>::right_open(cl_start, cl_end);
}

icl::discrete_interval<uint64_t> trace_event::block_range(void) const {
    // assume block size is 4KB
    auto erange = range();
    assert(erange.lower() < erange.upper());
    uint64_t block_start = erange.lower() & (~(BLOCK_SIZE - 1));
    uint64_t block_end = ( (erange.upper() - 1) & (~(BLOCK_SIZE - 1))) + BLOCK_SIZE;
    return icl::interval<uint64_t>::right_open(block_start, block_end);
}

string trace_event::str(void) const {
    stringstream ss;
    ios::fmtflags f(ss.flags());
    ss << event_type_to_str(type) ;
    switch (type)
    {
    case STORE:
        ss << ";" << std::hex<< "0x" << address << ";"
            << "0x" << value << ";"
            << "0x" << size;
        break;
    case FLUSH:
        ss << ";" << std::hex << "0x" << address << ";"
            << "0x" << size;
        break;
    case FENCE:
        break;
    case REGISTER_FILE:
        ss << ";" << file_path << ";"
            << std::hex << "0x" << address << ";"
            << "0x" << size << ";"
            << "0x" << file_offset;
        break;
    case WRITE:
        ss << ";" << file_path << ";" << size << ";" << buf;
        break;
    case PWRITEV:
        ss << ";" << file_path << ";"
            << wfile_offset << ";"
            << buf_vec.size() << ";";
        assert(buf_vec.size() > 0);
        for (size_t i = 0; i < buf_vec.size(); i++) {
            if (i != buf_vec.size() - 1) ss << buf_vec[i] << ";";
            else ss << buf_vec[i];
        }
        break;
    case FTRUNCATE:
        ss << ";" << file_path << ";" << len;
        break;
    case FALLOCATE:
        ss << ";" << file_path << ";" << mode
            << ";" << file_offset << ";" << len;
        break;
    case PWRITE64:
        ss << ";" << file_path << ";" << file_offset << ";"
            << size << ";" << char_buf;
        break;
    case WRITEV:
        ss << ";" << file_path  << ";"
            << iovcnt << ";";
        assert(iov.size() > 0);
        for (size_t i = 0; i < iov.size(); i++) {
            if (i != iov.size() - 1) ss << std::get<0>(iov[i]) << ";" << std::get<1>(iov[i]) << ";";
            else ss << std::get<0>(iov[i]) << ";" << std::get<1>(iov[i]);
        }
        break;
    case LSEEK:
        ss << ";" << file_path << ";" << file_offset << ";" << flags;
        break;
    case RENAME:
        ss << ";" << file_path << ";" << new_path;
        break;
    case OPEN:
        ss << ";" << file_path << ";" << flags << ";" << mode;
        break;
    case SYNC:
        break;
    case SYNC_FILE_RANGE:
        ss << ";" << file_path << ";" << file_offset << ";" << len << ";" << flags;
        break;
    case MSYNC:
        ss << ";" << file_path << ";" << address << ";" << size << ";" << flags;
        break;
    case UNREGISTER_FILE:
        ss << ";" << file_path << ";" << address << ";" << size;
        break;
    case PATHFINDER_BEGIN:
    case PATHFINDER_END:
    case PATHFINDER_OP_BEGIN:
    case PATHFINDER_OP_END:
        ss << raw;
        break;
    case CREAT:
    case MKDIR:
        ss << ";" << file_path << ";" << mode;
        break;
    case REGISTER_WRITE_FILE:
    case UNLINK:
    case FSYNC:
    case FDATASYNC:
    case CLOSE:
    case RMDIR:
    case SYNCFS:
    case READ:
    case PREAD:
        ss << ";" << file_path;
        break;
    default:
        cerr << "ERROR: unhandled case!\n";
        exit(1);
    }
    ss.flags(f);

    for (const stack_frame &sf : stack) {
        ss << ";" << sf.str();
    }

    ss.flush();
    return ss.str();
}


}  // namespace pathfinder