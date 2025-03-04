#pragma once

#include <cstdint>
#include <iostream>
#include <list>
#include <memory>
#include <mutex>
#include <regex>
#include <string>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <jinja2cpp/template.h>

#include <boost/filesystem.hpp>
#include <boost/filesystem/fstream.hpp>
#include <boost/process.hpp>
#include <boost/icl/interval_set.hpp>

extern "C" {
	#include <b64/cencode.h>
	#include <b64/cdecode.h>
}

#include "../utils/common.hpp"
#include "../utils/file_utils.hpp"
#include "../utils/util.hpp"
#include "stack_frame.hpp"
#include "trace_event.hpp"
#include "trace.hpp"


#define BUFFER_SIZE 1000000

namespace pathfinder
{

/**
 * A trace is, fundamentally, a list of events.
 */

typedef enum {
    PM, MMIO, POSIX
} pathfinder_mode;

class trace {
    /**
     * Private vars
     */
    bool selective_;
    pathfinder_mode mode_;
    std::string header_, footer_; // needed for trace reconstruction

    boost::icl::interval_set<uint64_t> testing_ranges_;
    std::list<uint64_t> testing_starts_;
    std::list<uint64_t> testing_stops_;

    // The files found in the "register file" commands in the trace that are not for WRITES.
    std::unordered_set<std::string> pm_files_;
    // The files used for file WRITEs
    std::unordered_set<std::string> write_files_;
    std::vector<std::shared_ptr<trace_event>> events_;
    std::vector<std::shared_ptr<trace_event>> stores_;
    uint64_t timestamp_ = 0;
    uint64_t store_num_ = 0;
    uint64_t write_num_ = 0;

    // Need a root folder for correct file mapping
    boost::filesystem::path root_dir;

    // For debug
    std::vector<std::string> intrinsic_functions;

    // For Pathfinder op tracing
    // Thread ID -> (op ID -> vector<uint64_t>(timestamp))
    std::unordered_map<uint64_t, std::unordered_map<int, std::vector<uint64_t>>> thread_ops_;
    std::optional<std::pair<uint64_t, int>> current_thread_op_;
    std::optional<std::pair<uint64_t, int>> current_tid_to_workload_tid_;

    std::vector<stack_frame> parse_pm_stack(std::vector<std::string>::iterator &start,
                                            std::vector<std::string>::iterator &end);

    std::vector<stack_frame> parse_posix_stack(std::vector<std::string> &raw_pieces);

    trace_event parse_pm_op(const std::string &raw_event);
    std::shared_ptr<char> base64_decode(const char* input, uint32_t size);
    trace_event parse_posix_op(const std::string &posix_event);

    size_t get_next_events(std::string line);

    void construct_testing_ranges(void);

public:
    trace(bool selective_testing, pathfinder_mode mode) : selective_(selective_testing), mode_(mode) {}

    void read(boost::process::child &child, std::istream &stream);
    void read(boost::process::child &child, boost::process::child &test, std::istream &stream);
    // for hse, I am just going to cheat and read trace offline
    void read_offline_trace(boost::filesystem::path trace_path);

    // setup root_dir
    void set_root_dir(boost::filesystem::path path) { root_dir = path; }

    // get root dir
    boost::filesystem::path get_root_dir(void) const { return root_dir; }

    const std::vector<std::shared_ptr<trace_event>> &events(void) const { return events_; }
    const std::vector<std::shared_ptr<trace_event>> &stores(void) const { return stores_; }

    bool within_testing_range(std::shared_ptr<trace_event> ptr) const;

    std::unordered_map<std::string, boost::filesystem::path> map_pmfile(
        boost::filesystem::path pmfile) const;

    std::unordered_map<std::string, boost::filesystem::path> map_pmfiles(
        boost::filesystem::path pmdir) const;

    // return a mapping from write files in trace to temp files in pmdir
    std::unordered_map<std::string, boost::filesystem::path> map_fsfiles(boost::filesystem::path pmdir) const;

    // when we have multiple files, use pmemcheck args as a hint to map files
    std::unordered_map<std::string, boost::filesystem::path> map_pmfile_hint(
        jinja2::ValuesMap checker_vals,
        jinja2::ValuesMap pmcheck_vals) const;

    std::unordered_map<std::string, boost::filesystem::path> get_fsfiles_hint(
        jinja2::ValuesMap checker_vals,
        jinja2::ValuesMap pmcheck_vals) const;

    void validate_store_events(void) const;

    void dump_csv(boost::filesystem::path &path) const;

    std::unordered_map<uint64_t, std::unordered_map<int, std::vector<uint64_t>>> get_thread_ops(void) const {
        return thread_ops_;
    }

    // for Pathfinder syscall decomposition
    void decompose_trace_events(void);

    // destructor for trace
    ~trace();
};

}