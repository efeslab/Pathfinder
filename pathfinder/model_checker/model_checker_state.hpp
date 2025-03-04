#pragma once

#include <boost/filesystem.hpp>
#include <algorithm>
#include <chrono>
#include <functional>
#include <future>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

#include "../graph/persistence_graph.hpp"
#include "../graph/pm_graph.hpp"
#include "../graph/posix_graph.hpp"
#include "../runtime/pathfinder_fs.hpp"
#include "../trace/trace.hpp"
#include "../utils/file_utils.hpp"
#include "../utils/util.hpp"


namespace pathfinder {

/**
 * @brief Maps timestamps to order in which they occur. -1 is not applied.
 *
 */
typedef std::map<uint64_t, int> event_config;

typedef std::vector<char> raw_data;

typedef std::list<std::shared_ptr<trace_event>> permutation_t;

static permutation_t operator+(const permutation_t &a, const permutation_t &b) {
    auto ait = a.begin();
    auto bit = b.begin();
    permutation_t res;

    while (ait != a.end() && bit != b.end()) {
        if ((*ait)->event_idx() < (*bit)->event_idx()) {
            res.push_back(*ait);
            ait++;
        } else {
            res.push_back(*bit);
            bit++;
        }
    }
    res.insert(res.end(), ait, a.end());
    res.insert(res.end(), bit, b.end());
    return res;
}

static permutation_t operator+=(permutation_t &p, const permutation_t &o) {
    p = p + o;
    return p;
}

typedef std::set<std::shared_ptr<trace_event>> event_set;

struct test_result {
    int ret_code = INT32_MAX;
    std::string output = "";
    std::string note = "";
    std::map<std::string, raw_data> file_images;

    bool contains_bug(void) const { return ret_code != 0; }

    bool valid(void) const { return ret_code != INT32_MAX; }
};


enum model_checker_code {
    NO_BUGS, HAS_BUGS, ALL_INCONSISTENT
};

// either do pathfinder test or other baseline tests
enum test_type {
    PATHFINDER, EXHAUSTIVE, LINEAR, FSYNC_TEST, FSYNC_REORDER_TEST
};

static void update_code(model_checker_code &res, model_checker_code newest) {
    switch (newest) {
        case NO_BUGS:
            return;
        case HAS_BUGS:
            if (res == NO_BUGS) res = HAS_BUGS;
            return;
        case ALL_INCONSISTENT:
            res = ALL_INCONSISTENT;
            return;
        default:
            std::cerr << "unhandled case!\n";
            exit(EXIT_FAILURE);
    }
}

static bool no_bugs(model_checker_code c) { return c == NO_BUGS; }
static bool has_bugs(model_checker_code c) { return c == HAS_BUGS || c == ALL_INCONSISTENT; }
static bool all_inconsistent(model_checker_code c) { return c == ALL_INCONSISTENT; }

/**
 * @brief This is a specific test instance
 *
 */
class model_checker_state {

    uint64_t test_case_id_ = 0;
    std::shared_ptr<std::mutex> file_mutex_;

    std::shared_ptr<std::mutex> stdout_mutex_;

    struct hash {
        uint64_t operator()(const boost::icl::discrete_interval<uintptr_t> &it) const {
            return std::hash<uintptr_t>{}(it.lower()) ^ std::hash<uintptr_t>{}(it.upper());
        }
    };

    std::unordered_map<
        boost::icl::discrete_interval<uintptr_t>,
        std::list<raw_data>,
        hash
    > checkpoints_;
    std::unordered_map<
        boost::icl::discrete_interval<uintptr_t>,
        std::list<boost::filesystem::path>,
        hash
    > checkpoint_files_;

    // map from file in model checker to range in model checker
    std::map<std::string, std::list<boost::icl::discrete_interval<uintptr_t>>> file_memory_regions_;

    // contains all address ranges that have been memory-mapped
    boost::icl::interval_set<uintptr_t> mapped_;

    // map from original range in the trace to range in the model checker
    std::unordered_map<
        boost::icl::discrete_interval<uintptr_t>,
        boost::icl::discrete_interval<uintptr_t>,
        hash
    > offset_mapping_;

    // for debugging
    // std::unordered_map<std::string, std::vector<std::pair<uint64_t, uint64_t>>> file_addr_map;
    // std::unordered_map<uint64_t, uint64_t> addr_offset_map;

    // for recovery from backup files
    std::list<std::shared_ptr<trace_event>> mmio_events;

    void record_file_images(test_result &res);

    /**
     * @brief Checkpoint the state of all PM files. Do this before running any tests.
     *
     */
    void checkpoint_push(void);

    /**
     * @brief Checkpoint the state of all PM files. Do this before running any tests.
     *
     */
    void checkpoint_pop(void);

    size_t num_checkpoints(void) const;

    /**
     * @brief Restore file to state before a test. Do this after running any test
     *
     */
    void restore(void);

    void sync_files(void);

    void wipe_files(void);

    void add_test_result(
        boost::filesystem::path result_file,
        const event_config &ec,
        const test_result &res,
        std::chrono::seconds timestamp);

    std::future<void> add_test_result_async(
        std::mutex &file_lock,
        boost::filesystem::path result_file,
        const event_config &ec,
        const test_result &res,
        std::chrono::seconds timestamp);


    bool store_is_redundant(std::shared_ptr<trace_event> te) const;
    // bool store_is_redundant(std::shared_ptr<trace_event> te, std::shared_ptr<trace_event> prior) const;

    test_result run_checker(void);

    model_checker_code test_possible_orderings(
        const event_config &init_config,
        const event_set &stores,
        std::string note);

    model_checker_code test_possible_orderings(
        const event_config &init_config,
        const event_set &stores,
        const event_set &syscalls,
        std::string note);

    std::set<permutation_t> generate_orderings(
        const event_set &stores,
        std::set<permutation_t> &already_tested) const;

    // to incorporate file writes in reordering
    std::set<permutation_t> generate_orderings(
        const event_set &stores,
        const event_set &writes,
        std::set<permutation_t> &already_tested) const;

    // to do exhaustive testing when generating permutation
    model_checker_code test_exhaustive_orderings(
        const event_config &init_config,
        const event_set &stores,
        const event_set &syscalls,
        std::string note="");
    // to do linear testing when generating perumutation
    model_checker_code test_linear_orderings(
        const event_config &init_config,
        const event_set &stores,
        const event_set &syscalls,
        std::string note="");

    template <typename C>
    void create_permutation(
        const C &events,
        event_config &curr);

    test_result test_permutation(std::string note);

    // debugging ordering print
    template <typename C>
    void print_orderings(const C &events, boost::filesystem::ofstream &odstream);

    // create backup files to store initial states before file writes
    void backup_write_files();
    // restore files to initial states
    void restore_write_files();
    // backup the pmdir with sparse file support
    void backup_pmdir(bool contains_sparse_file=false);
    // restore the pmdir with sparse file support
    void restore_pmdir(bool contains_sparse_file=false);
    // open files needed for writes
    void open_write_files();
    // close file descripters before running the checker
    void close_write_files();


    // for file WRITEs
    void do_write(std::shared_ptr<trace_event> te);
    // for file PWRITEVs
    void do_pwritev(std::shared_ptr<trace_event> te);
    // for file FTRUNCATEs
    void do_ftruncate(std::shared_ptr<trace_event> te);
    // for file FALLOCATEs
    void do_fallocate(std::shared_ptr<trace_event> te);
    // for file MSYNCs
    void do_msync(std::shared_ptr<trace_event> te);
    // for file PWRITE64s
    void do_pwrite64(std::shared_ptr<trace_event> te);
    // for file WRITEVs
    void do_writev(std::shared_ptr<trace_event> te);
    // for file LSEEKs
    void do_lseek(std::shared_ptr<trace_event> te);
    // for file RENAMEs
    void do_rename(std::shared_ptr<trace_event> te);
    // for file UNLINKs
    void do_unlink(std::shared_ptr<trace_event> te);
    // for file FSYNCs
    void do_fsync(std::shared_ptr<trace_event> te);
    // for file FDATASYNCs
    void do_fdatasync(std::shared_ptr<trace_event> te);
    // for file MUNMAPs
    void do_unregister_file(std::shared_ptr<trace_event> te);
    // for file OPENs
    void do_open(std::shared_ptr<trace_event> te);
    // for file CREATs
    void do_creat(std::shared_ptr<trace_event> te);
    // for file CLOSEs
    void do_close(std::shared_ptr<trace_event> te);
    // for RMDIRs
    void do_rmdir(std::shared_ptr<trace_event> te);
    // for MKDIRs
    void do_mkdir(std::shared_ptr<trace_event> te);
    // for SYNCs
    void do_sync(std::shared_ptr<trace_event> te);
    // for SYNCFSs
    void do_syncfs(std::shared_ptr<trace_event> te);
    // for SYNC_FILE_RANGEs
    void do_sync_file_range(std::shared_ptr<trace_event> te);

    bool do_store(std::shared_ptr<trace_event> te);

    boost::icl::discrete_interval<uintptr_t> do_register_file(
        std::shared_ptr<trace_event> te);

    // for Pin tool opt, output store list
    template <typename C>
    boost::filesystem::path output_stores(const C &event_list);
    // for Pin tool opt MMIO, run the Pin tool and get stores being read
    test_result run_recovery_observer_mmio(
        boost::filesystem::path stores_output,
        boost::filesystem::path pintool_output);
    // for Pin tool opt MMIO, run the Pin tool and get stores being read
    test_result run_recovery_observer_posix(
        boost::filesystem::path pintool_output);
    // for Pin tool opt MMIO, input a store list, and get a list of stores ids being read
    std::vector<int> get_accessed_stores(boost::filesystem::path pintool_output);

    // Test a permutation and get accessed stores via the pin tool
    test_result test_permutation(
        const std::set<std::shared_ptr<trace_event>> &stores,
        int perm_id,
        std::string note,
        std::vector<int>& accessed_stores);

    std::list<std::string> get_recovery_observer_mmio_args(
        boost::filesystem::path input_file_path,
        boost::filesystem::path output_file_path);

    std::list<std::string> get_recovery_observer_posix_args(
        boost::filesystem::path output_file_path);

    boost::filesystem::path construct_outdir_path(int perm_id, std::string suffix) const;

    boost::filesystem::path construct_outdir_path(std::string suffix, bool exists_ok=false) const;

    // replay until the begin of event_idxs, which is the first event of the test
    void setup_init_state(int until);

    test_result process_permutation(std::ostream& rstream, const std::vector<int>& perm, const std::string& test_type);

    // lseek helper functions called between file checkpoing and restore
    void record_lseek_offset(void);
    void apply_lseek_offset(void);

    // apply trace event, excluding register_file, store, flush and fence
    void apply_trace_event(std::shared_ptr<trace_event> te);
    
    // output workload operation completed to a file under pmdir / "ops_completed", this is for advanced checker when op_tracing is enabled
    void output_ops_completed(std::vector<uint64_t> all_applied_event_ids);

public:
    uint64_t test_id;
    const trace &event_trace;
    const persistence_graph *pg;

    const boost::filesystem::path outdir;
    std::vector<int> event_idxs;
    std::unordered_map<std::string, boost::filesystem::path> pmfile_map;
    // mapping from te:file to pathfinder:file
    std::unordered_map<std::string, boost::filesystem::path> fsfile_map;
    // mapping from te:file to pathfinder:fd
    std::unordered_map<std::string, int> file_to_fd;
    // mapping from te:fd to pathfinder:fd
    std::unordered_map<int, int> fd_to_fd;
    // mapping from writeable files to backup files
    std::unordered_map<std::string, boost::filesystem::path> backup_map;
    // map all trace fds to their lseek offset
    std::unordered_map<int, uint64_t> lseek_map;
    // use_pmdir
    boost::filesystem::path pmdir = boost::filesystem::path("");
    // backup pmdir
    boost::filesystem::path backup_dir = boost::filesystem::path("");
    // record start time
    std::chrono::time_point<std::chrono::system_clock> start_time;

    // pathfinder mode, could be PM, MMIO or POSIX
    pathfinder_mode mode_;

    // for op tracing
    bool op_tracing_;
    uint64_t prefix_event_id = 0;
    std::vector<uint64_t> applied_event_ids;

    // persevere flag
    bool persevere_;

    std::list<std::string> setup_args;
    std::list<std::string> daemon_args;
    std::list<std::string> checker_args;
    std::list<std::string> cleanup_args;

    bool save_file_images;
    bool map_direct;
    test_type ttype;
    std::chrono::seconds timeout;
    std::chrono::minutes baseline_timeout;

    // For POSIX, we no longer enumerate order in model_checker_state, and instead reply on PartialOrderGenerator
    std::vector<std::vector<int>> all_event_orders;

    // A filesystem wrapper for tracking synced items
    pathfinder_fs fs;

    std::optional<int> setup_until;


    model_checker_state(uint64_t id, const trace &t, boost::filesystem::path o, std::shared_ptr<std::mutex> m);

    void run_pm(std::promise<model_checker_code> &&res);

    void run_posix(std::promise<model_checker_code> &&res);

    void run_posix_with_orders(std::promise<model_checker_code> &&res);

    void run_sanity_check(std::promise<model_checker_code> &&res);

    const boost::filesystem::path &get_file_path(std::shared_ptr<trace_event> te) const {
        return pmfile_map.at(te->file_path);
    }
};

}