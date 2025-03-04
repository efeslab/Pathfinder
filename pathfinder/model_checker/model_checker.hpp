#pragma once

#include <boost/filesystem.hpp>
#include <chrono>
#include <functional>
#include <future>
#include <map>
#include <memory>
#include <mutex>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

#include <pthread.h>
#include <sched.h>

#include "model_checker_state.hpp"
#include "../graph/persistence_graph.hpp"
#include "../graph/pm_graph.hpp"
#include "../graph/posix_graph.hpp"
#include "../runtime/pathfinder_engine.hpp"
#include "../trace/trace.hpp"
#include "../utils/file_utils.hpp"
#include "../utils/util.hpp"


namespace pathfinder {

/**
 * @brief This class handles setting up individual tests.
 *
 */
class model_checker {

    const trace &trace_;
    const persistence_graph *pg_;
    boost::filesystem::path output_dir_;
    std::list<std::thread> threads_;
    test_type ttype_;
    pathfinder_mode mode_;
    bool op_tracing_;
    bool persevere_;
    uint64_t next_id_ = 0;
    std::chrono::seconds timeout_;

    std::shared_ptr<std::mutex> stdout_mutex_;

    /**
     * @brief Dump the trace information for all the trace events into a CSV
     * file in the output directory.
     *
     * Only dump stores.
     *
     */
    void dump_event_info(void) const;

    // common function for create_state
    model_checker_state* initialize_state(
    boost::filesystem::path pmfile,
    boost::filesystem::path pmdir,
    std::list<std::string> setup_args,
    std::list<std::string> checker_args,
    std::list<std::string> daemon_args,
    std::list<std::string> cleanup_args,
    jinja2::ValuesMap checker_vals,
    jinja2::ValuesMap pmcheck_vals,
    std::chrono::time_point<std::chrono::system_clock> start_time);

public:
    // TODO: this is error-pruning as users may forget to set the fields, should remove and set using constructor
    bool save_pm_images = false;
    std::vector<char> init_data;
    std::chrono::minutes baseline_timeout;

    model_checker(const trace &t, boost::filesystem::path outdir, std::chrono::seconds timeout=std::chrono::seconds(30), const persistence_graph *pg=nullptr, test_type ttype=PATHFINDER, pathfinder_mode mode=PM, bool op_tracing=false,bool persevere=false);

    /**
     * @brief Output the event file.
     *
     */
    void dump_event_file(void) const;

    /**
     * @brief For PM and MMIO. Create a model checker state to run
     */
    std::shared_ptr<model_checker_state> create_state(
        boost::filesystem::path pmfile,
        boost::filesystem::path pmdir,
        std::vector<int> event_idxs,
        std::list<std::string> setup_args,
        std::list<std::string> checker_args,
        std::list<std::string> daemon_args,
        std::list<std::string> cleanup_args,
        jinja2::ValuesMap checker_vals,
        jinja2::ValuesMap pmcheck_vals,
        std::chrono::time_point<std::chrono::system_clock> start_time,
        int setup_until=-1);

    /**
     * @brief For POSIX. Create a model checker state to run
     */
    std::shared_ptr<model_checker_state> create_state(
        boost::filesystem::path pmfile,
        boost::filesystem::path pmdir,
        std::vector<std::vector<int>> all_event_orders,
        std::list<std::string> setup_args,
        std::list<std::string> checker_args,
        std::list<std::string> daemon_args,
        std::list<std::string> cleanup_args,
        jinja2::ValuesMap checker_vals,
        jinja2::ValuesMap pmcheck_vals,
        std::chrono::time_point<std::chrono::system_clock> start_time);

    std::shared_future<model_checker_code> run_test(
        std::shared_ptr<model_checker_state> state);

    std::shared_future<model_checker_code> run_sanity_test(
        std::shared_ptr<model_checker_state> state);

    void join(void);
    void kill(void);

    int get_current_test_id(void) {
        
        return next_id_ - 1;
    }
};

}