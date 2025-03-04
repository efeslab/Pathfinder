#pragma once

#include <functional>
#include <string>
#include <iostream>
#include <tuple>

#include <boost/filesystem.hpp>
#include <boost/icl/interval.hpp>
#include <boost/program_options.hpp>
#include <boost/process.hpp>
#include <boost/iostreams/stream.hpp>
#include <boost/iostreams/tee.hpp>

#include <llvm/IR/Type.h>

#include <jinja2cpp/template.h>


#include <mlpack/core.hpp>
#include <mlpack/methods/dbscan/dbscan.hpp>


#include "../include/tree.hh"
#include "../utils/common.hpp"
#include "../utils/util.hpp"
#include "../model_checker/model_checker.hpp"
#include "../graph/persistence_graph.hpp"
#include "../graph/pm_graph.hpp"
#include "../graph/posix_graph.hpp"
#include "../trace/trace.hpp"
#include "../runtime/stack_tree.hpp"
#include "../runtime/pathfinder_engine.hpp"

namespace pathfinder
{
class model_checker;
class model_checker_state;


// used by MMIO and PM
typedef std::unordered_map<const llvm::Type*, std::vector<update_mechanism_group>> type_to_group_of_um_group;
// used by POSIX
typedef std::unordered_map<std::string, std::vector<update_mechanism_group>> function_to_group_of_um_group;

struct update_mechanism_hash {
    uint64_t operator()(const update_mechanism &g) const {
        uint64_t hash = 0;
        for (const vertex &v : g) {
            hash ^= std::hash<vertex>{}(v);
        }
        return hash;
    }
};

class sort_um_by_ts {
    const pm_graph &_graph;

public:
    sort_um_by_ts(const pm_graph &g) : _graph(g) {}

    bool operator()(const update_mechanism &lhs, const update_mechanism &rhs) const {
        if (lhs.empty()) return true;
        if (rhs.empty()) return false;
        if (_graph.get_event_idx(lhs.front()) < _graph.get_event_idx(rhs.front())) return true;
        else if (_graph.get_event_idx(lhs.front()) > _graph.get_event_idx(rhs.front())) return false;
        else return (lhs.size() < rhs.size());
    }
};

struct interval_hash {
    uint64_t operator()(const boost::icl::discrete_interval<uint64_t> &i) const {
        return std::hash<uint64_t>{}(i.lower()) ^ std::hash<uint64_t>{}(i.upper());
    }
};

class engine {
    boost::program_options::variables_map config_;
    boost::filesystem::path pmemcheck_path_;
    boost::filesystem::path pm_fs_path_;
    // These are the template values we can initialize once.
    jinja2::ValuesMap const_template_values_;
    // Store vals used in pmemcheck
    jinja2::ValuesMap pmcheck_vals_;
    // for all the IO subprocess things
    boost::asio::io_context io_context_;
    // record start time
    std::chrono::time_point<std::chrono::system_clock> start_time;

    int max_nproc_;

    int max_um_size_;

    pathfinder_mode mode_;

    bool op_tracing_;

    bool persevere_;

    boost::filesystem::path output_dir_;

    persistence_graph *pg_;

    /**
     * Gets a new set of template values (i.e., for things that need to be
     * temporary and/or exclusive and/or randomized).
     */
    jinja2::ValuesMap get_template_values(boost::filesystem::path seed_pmfile = "") const;

    /**
     * Gets the trace from the process (i.e., with pmemcheck).
     */
    trace gather_process_trace(void);

    /**
     * @brief Get all the update mechanisms by type and grouped by representatives.
     *
     */
    type_to_group_of_um_group get_update_mechanisms_by_type(const trace &t, const pm_graph &pg) const;

    /**
     * @brief Get the longest common prefix of two stack frames
     *        If no stack frame is same, return <"", -1>
     *        Else, return pair of longest common prefix and the index representing depth from the root
     */

    std::pair<std::string, int> get_longest_common_prefix(
    const std::vector<stack_frame> &a,
    const std::vector<stack_frame> &b) const;

    /**
     * @brief Get all the update mechanisms by function and grouped by representatives.
     *
     */
    function_to_group_of_um_group get_update_mechanisms_by_function(const trace &t, const posix_graph &pg) const;

    /**
     * @brief Split an instance graph into epochs
     *
     * @param full_g The full transitive closure graph.
     * @param iverts An instance graph, represented as a vector of vertices
     * @return std::vector<std::vector<vertex>> All epochs.
     */
    std::vector<update_mechanism> split_by_epochs(
        const llvm::Type *instance_type,
        const graph_type &g,
        const std::vector<vertex> &iverts) const;

    /**
     * @brief Put the representative update mechanism first, by data type.
     *
     * @param full_g
     * @param partial_g
     * @param mechanisms
     */
    std::vector<update_mechanism_group> group_by_representative_in_type(
        const llvm::Type *t,
        const graph_type &g,
        std::vector<update_mechanism> &mechanisms) const;

    /**
     * @brief Given an arbitrary node in the stack tree, group all its update mechanisms and all its children's update mechanisms. Then splits into new update mechanisms by clustering.
     * 
     * @param child_iterator
     * @param stack_tree
     * @return pair<parent.function, update_mechanism_group>
    */
    update_mechanism_group group_by_parent_child(
        const tree<std::shared_ptr<stack_tree_node>>::iterator child_it,
        stack_tree *st) const;

    /**
     * @brief Splits update mechanisms into sets of (almost) contiguous update mechanisms using clustering algorithm.
     * 
     * @param update_mechanism
    */
    update_mechanism_group split_by_clustering(
        const update_mechanism &um) const;

    /**
     * @brief Given an update mechanism, make it contiguous by filling in events from other threads.
     * 
     * @param update_mechanism
    */
    update_mechanism generate_continuous_update_mechanism(
        const update_mechanism &um) const;

    /**
     * @brief Check if the small update mechanism is an induced subgraph of the large update mechanism within the update protocol defined by function.
     * 
     * @param function
     * @param large
     * @param small
     */
    bool is_induced_subgraph_in_function(
        std::string function,
        const update_mechanism &large,
        const update_mechanism &small) const;

    /**
     * @brief Put the representative update mechanism first, by function (update protocol).
     *
     * @param function
     * @param mechanisms
     */
    std::vector<update_mechanism_group> group_by_representative_in_function(
        std::string function,
        std::vector<update_mechanism> &mechanisms) const;

    /**
     * @brief Get the representative update mechanism for a given update mechanism.
     * 
     * @param fmap 
     */
    void run_representative_testing_by_function(const trace &t, function_to_group_of_um_group &fmap);

    /**
     * @brief For PM and MMIO. Create a test object using the templated configuration. 
     *
     * @param checker Model checker instance, constructs a state object
     * @param graph Used to get event indices, type is pm_graph
     * @param mechanism The update mechanism to test
     * @return std::shared_ptr<model_checker_state> A state object
     */
    std::shared_ptr<model_checker_state> create_test(
        model_checker &checker,
        const pm_graph &graph,
        const update_mechanism &mechanism) const;

    /**
     * @brief For POSIX. Create a test object using the templated configuration.
     *
     * @param checker Model checker instance, constructs a state object
     * @param pg Used to generate all orderings, type is posix_graph
     * @param vertex_vec The vertices in the target subgraph
     * @return std::shared_ptr<model_checker_state> A state object
     */
    std::shared_ptr<model_checker_state> create_test(
        model_checker &checker,
        posix_graph &graph,
        std::vector<vertex> &vertex_vec);

    /**
     * @brief For exhaustive testing, create a test object specified by index range.
     *
     * @param checker Model checker instance, constructs a state object
     * @param start_idx Start index of the reorder range
     * @param end_idx End index of the reorder range
     * @return std::shared_ptr<model_checker_state> A state object
     */
    std::shared_ptr<model_checker_state> create_test(
        model_checker &checker,
        std::vector<int> event_idxs,
        int setup_until=-1) const;

    /**
     * @brief For sanity testing, create a test object.
     *
     * @param checker Model checker instance, constructs a state object
     * @return std::shared_ptr<model_checker_state> A state object
     */
    std::shared_ptr<model_checker_state> create_sanity_test(
        model_checker &checker) const;

    /* --- utility methods --- */

    bool config_enabled(const char *key) const { return config_[key].as<bool>(); }
    bool config_not_empty(const char *key) const { return !config_[key].as<std::string>().empty(); }
    bool config_is_empty(const char *key) const { return !config_not_empty(key); }
    int config_int(const char *key) const { return config_[key].as<int>(); }

    void try_fill_args(
        const jinja2::ValuesMap &vals,
        std::list<std::string> &args,
        const char *key) const;

    void fill_args(
        const jinja2::ValuesMap &vals,
        std::list<std::string> &args,
        const char *key) const;

    std::string resolve_config_value(const char *key) const;

    std::string resolve_config_value(const jinja2::ValuesMap &vals,
                                     const char *key) const;

    std::vector<char> setup_file_data_;

    // called by run(), performs random testing instead of pathfindering
    int run_random_testing(pm_graph &graph,
                           model_checker &checker,
                           int &total_tests,
                           int &num_bugs,
                           boost::iostreams::stream<
                              boost::iostreams::tee_device<std::ostream, std::ofstream>
                           > &tout);

    // called by run(), run linear, exhaustive, or jaaru-style testing
    int run_baseline_testing(
        const trace &full_trace,
        boost::filesystem::path output_dir,
        boost::iostreams::stream<
            boost::iostreams::tee_device<std::ostream, std::ofstream>
        > &tout);

    void analyze_trace(boost::filesystem::path output_dir, const llvm::Module &m, const trace &t);
    void output_trace(boost::filesystem::path output_dir, trace t);
    void output_groups(boost::filesystem::path output_dir, const pm_graph &graph, const std::vector<update_mechanism_group> &groups);

    void finalize_results(
        const boost::filesystem::path &final_out,
        const boost::filesystem::path &tmp_out,
        const boost::filesystem::path &success_file) const;

public:
    engine(const boost::program_options::variables_map &config);

    /**
     * Run Pathfinder.
     *
     * Return: 0 on success, error code on failure.
     */
    int run(void);
};

}  // namespace pathfinder
