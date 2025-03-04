#pragma once

#include <sys/types.h>
#include <cstdint>
#include <string>

// this is for decomposed trace events
// currently the purpose of decomposing trace events is mainly for easier deriving dependencies between syscalls

namespace pathfinder {

enum micro_event_type {
    DATA_WRITE, // data update
    SETATTR, // metadata update
    INODE_DIR_WRITE, // inode data block write, treat as a separate type
    ADD_FILE_INODE, // create file inode
    ADD_DIR_INODE, // create dir inode
};

struct micro_event {
    micro_event_type type;
    std::string file_path;
    off_t file_offset;
    uint64_t write_size;

    micro_event() {}

    bool is_data_update() const {
        return type == DATA_WRITE;
    }
    bool is_metadata_update() const {
        return type == SETATTR;
    }
    bool is_inode_data_update() const {
        return type == INODE_DIR_WRITE;
    }
    bool is_add_file_inode() const {
        return type == ADD_FILE_INODE;
    }
    bool is_add_dir_inode() const {
        return type == ADD_DIR_INODE;
    }
};

namespace m_event {

inline micro_event creat_data_update(const std::string &file_path, off_t file_offset, uint64_t size) {
    micro_event event;
    event.type = DATA_WRITE;
    event.file_path = file_path;
    event.file_offset = file_offset;
    event.write_size = size;
    return event;
}

inline micro_event creat_metadata_update(const std::string &file_path) {
    micro_event event;
    event.type = SETATTR;
    event.file_path = file_path;
    return event;
}

inline micro_event creat_inode_data_update(const std::string &file_path) {
    micro_event event;
    event.type = INODE_DIR_WRITE;
    event.file_path = file_path;
    return event;
}

inline micro_event creat_add_file_inode(const std::string &file_path) {
    micro_event event;
    event.type = ADD_FILE_INODE;
    event.file_path = file_path;
    return event;
}

inline micro_event creat_add_dir_inode(const std::string &file_path) {
    micro_event event;
    event.type = ADD_DIR_INODE;
    event.file_path = file_path;
    return event;
}

}  // namespace m_event

}  // namespace pathfinder