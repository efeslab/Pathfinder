#pragma once

#include <cassert>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include <boost/icl/interval_map.hpp>

#include "../utils/common.hpp"
#include "stack_frame.hpp"
#include "micro_event.hpp"

namespace pathfinder
{

enum event_type {
    /*
    All the PM operations:
        - Store
        - Flush
        - Fence

    Some operations are compound:
        - MOVNT: store + flush
        - CLFLUSH: flush + LOCAL fence
    */
    STORE, FLUSH, FENCE,
    /* All the PM assertions. We generally ignore these. */
    ASSERT_PERSISTED, ASSERT_ORDERED, REQUIRED_FLUSH,
    /* Marks the file being modified. */
    REGISTER_FILE,
    /* End of the trace, i.e., there are no more events! */
    STOP,
    /* File writes opened by syscall write  */
    WRITE,
    /* Files opened for writes */
    REGISTER_WRITE_FILE,
    /* File writes opened by syscall pwritev */
    PWRITEV,
    /* For ftruncate syscall */
    FTRUNCATE,
    /* For fallocate syscall */
    FALLOCATE,
    /* For Pathfinder's selective testing. */
    PATHFINDER_BEGIN, PATHFINDER_END,
    /* For POSIX. */
    UNREGISTER_FILE,
    MSYNC,
    PWRITE64,
    WRITEV,
    LSEEK,
    RENAME,
    UNLINK,
    FSYNC,
    FDATASYNC,
    OPEN,
    CREAT,
    CLOSE,
    MKDIR,
    RMDIR,
    SYNC,
    SYNCFS,
    SYNC_FILE_RANGE,
    /* Read family calls*/
    READ,
    PREAD,
    /* For Pathfinder's OP Tracing. */
    PATHFINDER_OP_BEGIN, PATHFINDER_OP_END,
};

const char *event_type_to_str(event_type t);

/**
 *
 */
struct trace_event {
    std::string raw;
    uint64_t timestamp;
    uint64_t store_num = UINT64_MAX;
    uint64_t write_num = UINT64_MAX;

    event_type type;
    uint64_t address;
    uint64_t size;
    // We don't need this for update mechanisms, but we do need it for testing.
    std::vector<char> value_bytes;
    uint64_t value;
    // For REGISTER FILE
    std::string file_path;
    off_t file_offset;

    // For syscall file WRITEs
    std::string buf;

    // For syscall file PWRITEVs
    std::vector<std::string> buf_vec;
    uint64_t wfile_offset;

    // For syscall file FTRUNCATEs & FALLOCATEs
    off_t len;
    int mode;

    // For POSIX store
    uint64_t tid;
    std::shared_ptr<char> char_buf;

    // For POSIX mmap & msync & lseek
    int flags;
    int prot;

    // For POSIX writev
    std::vector<std::tuple<int, std::shared_ptr<char>>> iov;
    int iovcnt;

    // For RENAME
    std::string new_path;
    std::string old_path;

    // For open O_APPEND
    long int file_size = -1;

    // For any events that need block range
    std::optional<std::pair<int, int>> block_ids;

    // fd
    int fd = -1;

    // For Pathfinder op tracing
    // we need workload thread id so that later to inform checker which operations are completed
    std::optional<int> workload_thread_id;
    std::optional<int> thread_op_id;

    // For Pathfinder syscall decomposition
    std::optional<std::vector<micro_event>> micro_events;

    std::vector<stack_frame> stack;

    trace_event() {}

    bool is_valid(void) const { return type != STOP; }
    bool is_store(void) const { return type == STORE; }
    bool is_flush(void) const { return type == FLUSH; }
    bool is_fence(void) const { return type == FENCE; }
    bool is_register_file(void) const { return type == REGISTER_FILE; }
    bool is_write(void) const { return type == WRITE; }
    bool is_register_write_file(void) const { return type == REGISTER_WRITE_FILE; }
    bool is_pwritev(void) const { return type == PWRITEV; }
    bool is_ftruncate(void) const { return type == FTRUNCATE; }
    bool is_fallocate(void) const { return type == FALLOCATE; }
    bool is_pathfinder_begin(void) const { return type == PATHFINDER_BEGIN; }
    bool is_pathfinder_end(void) const { return type == PATHFINDER_END; }
    bool is_unregister_file(void) const { return type == UNREGISTER_FILE; }
    bool is_msync(void) const { return type == MSYNC; }
    bool is_pwrite64(void) const { return type == PWRITE64; }
    bool is_writev(void) const { return type == WRITEV; }
    bool is_lseek(void) const { return type == LSEEK; }
    bool is_rename(void) const { return type == RENAME; }
    bool is_unlink(void) const { return type == UNLINK; }
    bool is_fsync(void) const { return type == FSYNC; }
    bool is_fdatasync(void) const { return type == FDATASYNC; }
    bool is_open(void) const { return type == OPEN; }
    bool is_creat(void) const { return type == CREAT; }
    bool is_close(void) const { return type == CLOSE; }
    bool is_mkdir(void) const { return type == MKDIR; }
    bool is_rmdir(void) const { return type == RMDIR; }
    bool is_sync(void) const { return type == SYNC; }
    bool is_syncfs(void) const { return type == SYNCFS; }
    bool is_sync_file_range(void) const { return type == SYNC_FILE_RANGE; }
    bool is_sync_family(void) const { return is_fsync() || is_fdatasync() || is_sync() || is_syncfs() || is_sync_file_range(); }
    bool is_write_family(void) const { return is_write() || is_pwritev() || is_pwrite64() || is_writev(); }
    bool is_read(void) const { return type == READ; }
    bool is_pread(void) const { return type == PREAD; }
    bool is_pathfinder_op_begin(void) const { return type == PATHFINDER_OP_BEGIN; }
    bool is_pathfinder_op_end(void) const { return type == PATHFINDER_OP_END; }
    bool is_marker_event(void) const { return type == PATHFINDER_BEGIN || type == PATHFINDER_END || type == PATHFINDER_OP_BEGIN || type == PATHFINDER_OP_END; }

    /**
     * Return the address range that the event modifies.
     */
    boost::icl::discrete_interval<uint64_t> range(void) const;

    boost::icl::discrete_interval<uint64_t> cacheline_range(void) const;

    boost::icl::discrete_interval<uint64_t> block_range(void) const;

    operator bool(void) const { return is_valid(); }

    std::string str(void) const;

    int event_idx(void) const { return (int)timestamp; }
    uint64_t store_id(void) const {
        assert(is_store());
        return store_num;
    }

    uint64_t write_id(void) const {
        assert(is_write() || is_pwritev() || is_pwrite64() || is_writev());
        return write_num;
    }

    uint64_t thread_id(void) const {
        return tid;
    }

    int workload_tid(void) const {
        assert(workload_thread_id.has_value());
        return workload_thread_id.value();
    }

    std::vector<stack_frame> backtrace(void) const {
        return stack;
    }
};

}  // namespace pathfinder