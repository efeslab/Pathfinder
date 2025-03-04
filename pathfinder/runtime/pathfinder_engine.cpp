#include "pathfinder_engine.hpp"

#include <algorithm>
#include <numeric>
#include <chrono>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <list>
#include <sstream>
#include <memory>
#include <boost/algorithm/string.hpp>
#include <boost/algorithm/string/regex.hpp>
#include <boost/iostreams/tee.hpp>
#include <boost/iostreams/stream.hpp>
#include <boost/filesystem.hpp>
#include <boost/graph/transitive_closure.hpp>
#include <jinja2cpp/template.h>

#include <llvm/IR/Module.h>
#include <llvm/IRReader/IRReader.h>
#include <llvm/IR/LLVMContext.h>
#include <llvm/Support/SourceMgr.h>

#define DEBUGGING 1
// #define UM_MAX_SIZE 100
#define UM_CHUNK_SIZE 8
#define UM_CHUNK_GAP 4
#define LINEAR_MAX_SIZE 100
#define MAX_HEIGHT 6
#define POLL_MILLIS 1000

namespace bio = boost::iostreams;
namespace bp = boost::process;
namespace fs = boost::filesystem;
namespace icl = boost::icl;
namespace po = boost::program_options;
using namespace jinja2;
using namespace llvm;
using namespace std::chrono;
using namespace std;


namespace pathfinder
{

static const update_mechanism &get_representative(const update_mechanism_group &g) {
    return g.at(0);
}

/* engine */

string engine::resolve_config_value(const char *key) const {
    auto map = get_template_values();
    return resolve_config_value(map, key);
}

string engine::resolve_config_value(const jinja2::ValuesMap &vals,
                                    const char *key) const {
    Template tmpl;
    tmpl.Load(config_[key].as<string>());
    return tmpl.RenderAsString(vals).value();
}

engine::engine(const po::variables_map &config) :
    config_(config),
    pmemcheck_path_(PMEMCHECK_PATH),
    pm_fs_path_(config["general.pm_fs_path"].as<fs::path>()),
    max_nproc_(config["general.max_nproc"].as<int>())
{
    /*
    Do a couple of sanity checks.
    */
    if(!fs::exists(pmemcheck_path_) || !fs::is_regular_file(pmemcheck_path_)) {
        cerr << "Err: " << pmemcheck_path_.string() << " must exist!\n";
        exit(EXIT_FAILURE);
    }
    pmemcheck_path_ = fs::canonical(pmemcheck_path_);

    assert(fs::exists(pm_fs_path_) && fs::is_directory(pm_fs_path_));
    pm_fs_path_ = fs::canonical(pm_fs_path_);

    fs::path build_root = fs::path(BUILD_ROOT);
    assert(fs::exists(build_root) && fs::is_directory(build_root));
    build_root = fs::absolute(build_root);

    if (max_nproc_ <= 0) {
        cerr << "Invalid nproc: " << max_nproc_ << "\n";
        exit(EXIT_FAILURE);
    }

    /*
    Primary task here is to fill in the template values.
    */
    const_template_values_["pwd"] = config["general.pwd"].as<fs::path>().string();
    const_template_values_["build_root"] = build_root.string();

    // Set Pathfinder mode
    if (config_["general.mode"].as<string>() == "pm") {
        mode_ = PM;
    }
    else if (config_["general.mode"].as<string>() == "mmio") {
        mode_ = MMIO;
    }
    else if (config_["general.mode"].as<string>() == "posix") {
        mode_ = POSIX;
    }
    else {
        cerr << "Error: unknown mode '" << config_["general.mode"].as<string>() << "'\n";
        exit(EXIT_FAILURE);
    }

    op_tracing_ = config["general.op_tracing"].as<bool>();
    persevere_ = config["general.persevere"].as<bool>();
    max_um_size_ = config["general.max_um_size"].as<int>();
}

ValuesMap engine::get_template_values(fs::path seed_pmfile) const {
    ValuesMap vals = const_template_values_;

    fs::path pmfile, pmdir;
    do {
        pmdir = pm_fs_path_ / fs::unique_path();
    } while (fs::exists(pmdir));
    vals["pmdir"] = pmdir.string();

    if (!seed_pmfile.empty()) {
        pmfile = pmdir / seed_pmfile.filename();
    } else {
        pmfile = pmdir / fs::unique_path();
    }
    error_if_exists(pmfile);
    vals["pmfile"] = pmfile.string();

    fs::path tmpfile;
    do {
        tmpfile = fs::path("/tmp") / fs::unique_path();
    } while (fs::exists(tmpfile));
    vals["tmpfile"] = tmpfile.string();

    // add naive support for cmdline multiple files, fill in pmfile1, pmfile2, ...
    for (int i=0; i < config_["general.num_cmdfiles"].as<int>(); i++) {
        fs::path tmp_pmfile = pmfile.parent_path() / (pmfile.filename().string() + to_string(i));
        error_if_exists(tmp_pmfile);

        vals["pmfile"+to_string(i)] = tmp_pmfile.string();
    }

    vals["port"] = std::to_string(get_open_port());
    // musa: added this to pass to a test script
    // so a test can connect to a server's on it's admin port
    vals["admin_port"] = std::to_string(get_open_port());

    return vals;
}

trace engine::gather_process_trace(void) {
    // Get the arguments templated out.
    ValuesMap vals = get_template_values();
    pmcheck_vals_ = vals;

    string trace_path_str = config_["trace.trace_path"].as<string>();
    if (!trace_path_str.empty()) {
        fs::path trace_path = fs::path(resolve_config_value(vals, "trace.trace_path"));
        // copy the log file to output_dir
        fs::copy_file(trace_path, output_dir_ / "tracer.log");
        trace prog_trace(config_enabled("general.selective_testing"), mode_);
        prog_trace.read_offline_trace(trace_path);
        string root_dir_str = config_["trace.root_dir"].as<string>();
        assert(!root_dir_str.empty() && "Root dir must be set for offline traces");
        prog_trace.set_root_dir(fs::path(root_dir_str));
        #if DEBUGGING
            prog_trace.validate_store_events();
        #endif
        prog_trace.decompose_trace_events();
        return prog_trace;
    }

    string argstr;
    if (config_not_empty("trace.daemon_tmpl")){
        argstr = resolve_config_value(vals, "trace.daemon_tmpl");
    } else {
        argstr = resolve_config_value(vals, "trace.cmd_tmpl");
    }

    // prepare initial file states before running pmemcheck
    fs::path init_state_path = config_["general.init_file_state"].as<fs::path>();
    if (!init_state_path.empty()) {
        cerr << __FILE__ << " @ line " << __LINE__ <<
            ": argument \"general.init_file_state\" is depreciated (" <<
            init_state_path << " was provided)\n";
    }
    fs::path pmdir(vals["pmdir"].asString());
    create_directories_or_error(pmdir);

    list<string> prog_args;
    // should split by any number of spaces
    boost::split(prog_args, argstr, boost::is_any_of(" "), boost::token_compress_on);
    
    list<string> tracer_args;
    bp::pipe p;
    // get current time in a string
    std::time_t t = std::time(nullptr);
    std::tm* now = std::localtime(&t);
    std::stringstream ss;
    ss << std::put_time(now, "%Y-%m-%d-%H-%M-%S");
    std::string time_str = ss.str();

    // generate a temporary log path
    string log_path = "/tmp/" + time_str + ".log";
    if (fs::exists(fs::path(log_path))) {
        fs::remove(fs::path(log_path));
    }
    if (mode_ == PM) {
        // -- add the pmemcheck arguments
        list<string> pmchk_args = {
            "--tool=pmemcheck", "-q", "--log-stores=yes", "--isa-rec=yes",
            "--log-stores-stacktraces=yes", "--trace-children=yes",
            "--print-summary=no"
        };

        pmchk_args.push_back("--flush-check=no");


        Template log_arg_tmpl;
        log_arg_tmpl.Load("--log-fd={{ sink_fd }}");
        ValuesMap log_vals;
        log_vals["sink_fd"] = to_string(p.native_sink());
        string log_arg = log_arg_tmpl.RenderAsString(log_vals).value();
        pmchk_args.push_back(log_arg);

        // -- this is now all the arguments
        pmchk_args.insert(pmchk_args.end(), prog_args.begin(), prog_args.end());

        cout << endl << pmemcheck_path_.string();
        for (const auto &arg : pmchk_args) {
            cout << " " << arg;
        }
        cout << endl;
        tracer_args = pmchk_args;
    }
    else {
        list<string> pin_args = {
            "-follow-execv", "-t", PINTOOL_TRACER_PATH, "-o", log_path, "-tf", vals["pmdir"].asString(), "--"
        };
        pin_args.insert(pin_args.end(), prog_args.begin(), prog_args.end());
        tracer_args = pin_args;
    }

    // --- If there is setup, run it now.
    if (config_not_empty("trace.setup_tmpl")) {
        list<string> setup_args;
        boost::split(setup_args,
            resolve_config_value(vals, "trace.setup_tmpl"),
            boost::is_space());

        bp::child daemon;
        bp::ipstream dout, derr;
        if (config_not_empty("trace.setup_daemon_tmpl")) {
            list<string> setup_daemon_args;
            boost::split(setup_daemon_args,
                resolve_config_value(vals, "trace.setup_daemon_tmpl"),
                boost::is_space());
            daemon = start_command(setup_daemon_args, dout, derr);
        }

        string output;
        int ret = run_command(setup_args, output);
        if (0 != ret) {
            cerr << "Setup failed! ret=" << ret << "\n" << output << "\n";
            exit(EXIT_FAILURE);
        }

        if (daemon.valid()) {
            string doutput;
            ret = finish_command(daemon, dout, derr, doutput);
            if (0 != ret) {
                cerr << "Setup daemon failed! ret=" << ret << "\n" << doutput << "\n";
                exit(EXIT_FAILURE);
            }
        }

        // We also have to pass this setup state to the checker.
        std::ifstream input( vals.at("pmfile").asString(), std::ios::binary );
        std::vector<char> buffer(std::istreambuf_iterator<char>(input), {});
        setup_file_data_ = buffer;
    }

    // Start the process
    bp::child c;
    cout << "tracer args: ";
    for (const auto & tracer_arg : tracer_args) {
        cout << tracer_arg << " ";
    }
    cout << endl;
    if (mode_ == PM) {
        if (config_enabled("trace.verbose")) {
            c = bp::child(pmemcheck_path_, get_pm_env(), bp::args(tracer_args));
        } else {
            c = bp::child(pmemcheck_path_, get_pm_env(), bp::args(tracer_args),
                bp::std_err > bp::null, bp::std_out > bp::null);
        }
    }
    else {
        if (config_enabled("trace.verbose")) {
            c = bp::child(PIN_PATH, boost::this_process::environment(), bp::args(tracer_args));
        } else {
            c = bp::child(PIN_PATH, boost::this_process::environment(), bp::args(tracer_args),
                bp::std_err > bp::null, bp::std_out > bp::null);
        }        
    }


    // Now we can start reading in the trace
    trace prog_trace(config_enabled("general.selective_testing"), mode_);

    prog_trace.set_root_dir(fs::path(vals["pmdir"].asString()));

    // If we are running a daemon, need to now start the testing command
    bp::child test;
    bp::ipstream tout, terr;
    if (config_not_empty("trace.cmd_tmpl") && config_not_empty("trace.daemon_tmpl")) {
        list<string> cmd_args;
        boost::split(cmd_args,
            resolve_config_value(vals, "trace.cmd_tmpl"),
            boost::is_space());
        // for (const string &a : cmd_args) {
        //     cerr << "[CMD ARG] " << a << endl;
        // }
        // Wait for daemon to start up
        c.wait_for(chrono::seconds(10));
        test = start_command(cmd_args, tout, terr);
    }

    if (mode_ == PM) {
        close(p.native_sink());

        // Now get a stream to read from the valgrind server.
        bp::ipstream stream;
        stream.pipe(p);
        if (test.valid()) {
            prog_trace.read(c, test, stream);
        } else {
            prog_trace.read(c, stream);
        }
    }
    else {
        // wait until child c is ready
        // don't need to do it for pmemcheck
        c.wait();
        std::ifstream stream(log_path);
        if (test.valid()) {
            prog_trace.read(c, test, stream);
        } else {
            prog_trace.read(c, stream);
        }
    }

    if (test.valid()) {
        string output;
        int ret = finish_command(test, tout, terr, output);
        if (0 != ret) {
            cerr << "Test command failed! ret=" << ret << "\n" << output << "\n";
            exit(EXIT_FAILURE);
        }
    }
    // copy the log file to output_dir
    fs::copy_file(fs::path(log_path), output_dir_ / "tracer.log");
    // remove the log file
    fs::remove(fs::path(log_path));

    // --- If there is cleanup, run it now.
    if (config_not_empty("trace.cleanup_tmpl")) {
        list<string> cleanup_args;
        boost::split(cleanup_args,
            resolve_config_value(vals, "trace.cleanup_tmpl"),
            boost::is_space());
        string output;
        int ret = run_command(cleanup_args, output);
        if (0 != ret) {
            cerr << "Setup failed! ret=" << ret << "\n" << output << "\n";
            exit(EXIT_FAILURE);
        }
    }

    if (fs::exists(fs::path(vals.at("pmfile").asString()))) {
        fs::remove(fs::path(vals.at("pmfile").asString()));
    }

    for (int i=0; i<config_["general.num_cmdfiles"].as<int>(); i++) {
        if (fs::exists(fs::path(vals.at("pmfile"+to_string(i)).asString())))
            fs::remove(fs::path(vals.at("pmfile"+to_string(i)).asString()));
    }

    // clean-up pmdir used to generate trace
    fs::remove_all(fs::path(vals["pmdir"].asString()));
    BOOST_ASSERT(!fs::exists(fs::path(vals["pmdir"].asString())));

    #if DEBUGGING
        prog_trace.validate_store_events();
    #endif

    prog_trace.decompose_trace_events();
    return prog_trace;
}

vector<update_mechanism> engine::split_by_epochs(
    const Type *instance_type,
    const graph_type &g,
    const vector<vertex> &iverts) const
{
    // size_t MAX_RANGE = SIZE_MAX;
    // unordered_set<vertex> ivert_set(iverts.begin(), iverts.end());
    const_property_map pmap = boost::get(pnode_property_t(), g);

    #ifdef DEBUG_MODE
    unordered_set<vertex> check(iverts.begin(), iverts.end());
    assert(check.size() == iverts.size() && "Shouldn't have duplicates!");
    #endif

    /**
     * @brief This is where we do all the magic heuristics.
     *
     * Originally in pmcc's "split_into_epochs"
     */

    /**
     * @brief Step 1: check for interruptions
     *
     * An interruption ocurrs if, between two sequential (i.e., in program order)
     * operations that have a path do not have a direct edge to each other.
     */

    vector<int> split_idxs;
    // split_idxs.push_back(0);
    // set<int> split_idx_set;

    for (int i = 0; i < iverts.size() - 1; ++i) {
        const vertex &a = iverts[i];
        const pm_node *na = dynamic_cast<const pm_node*>(get(pmap, a));

        const vertex &b = iverts[i+1];
        const pm_node *nb = dynamic_cast<const pm_node*>(get(pmap, b));

        assert(na && nb && "Should have a node for each vertex!");

        // First, check if these two nodes are connected.
        // If not, then keep going.

        // else, Split if the gap between stores is bigger than the data structure
        size_t store_gap = nb->event()->store_id() - na->event()->store_id();

        if (has_path(g, a, b)) {
            // Now, if they aren't connected, that means there's an interruption,
            // and we should split the graph.
            auto res = boost::edge(a, b, g);
            if (!res.second) {
                // Non-inclusive end range.
                split_idxs.push_back(i);
                continue;
            }
        }

        if (store_gap > na->type_size(instance_type)) {
            split_idxs.push_back(i);
            continue;
        }

    }

    // Create the slices.
    vector<update_mechanism> interrupted_epochs = split_vector(iverts, split_idxs);

    BOOST_ASSERT_MSG(interrupted_epochs.size() >= 1, "Have to have at least one epoch!");

    /**
     * @brief Step 2: Split on repeatedly modified fields
     *
     *  Now sometimes, we can still get very long sequences of operations when a
        specific data structure is always updated atomically with others; for example,
        the level hashing root structure is updated with other buckets a lot, resulting
        in thousands of events in a single test. But these data structures have finite
        elements. So, we can also split on repeated updates to the same field.
        Basically, if foo->A is modified, then a bunch of stuff, then foo->A again,
        we split into another epoch after foo->A. How do we select A? Probably
        by picking the most often modified field in foo.
        In theory, we could also repeatedly split until the field count is at
        most one, but that seems not so good.
        We also want to make sure we aren't splitting on sequential updates to
        the same field (i.e., while(...) something++; )
     *
     * The rest of these steps operate on partially-create epochs.
     *
     */
    vector<update_mechanism> repetition_epochs;
    for (const update_mechanism &epoch : interrupted_epochs) {
        vector<int> split_idxs;

        const pm_node *last_node = dynamic_cast<const pm_node*>(boost::get(pmap, epoch[0]));
        assert(last_node);
        for (int x = 1; x < epoch.size(); ++x) {
            const pm_node *node = dynamic_cast<const pm_node*>(boost::get(pmap, epoch[x]));
            assert(node);
            // the store num check is a check against successive, tmp updates.
            // -- Don't do this for array types,
            if (node->field(instance_type) == last_node->field(instance_type)
                && (node->event()->store_id() - last_node->event()->store_id() > 1
                    || node->field_is_array_type(instance_type))) {
                split_idxs.push_back(x - 1);
            }

            last_node = node;
        }

        // Create the slices.
        auto sliced = split_vector(epoch, split_idxs);
        repetition_epochs.insert(repetition_epochs.end(), sliced.begin(), sliced.end());
    }

    assert(repetition_epochs.size() >= 1 && "Have to have at least one epoch!");

    /**
     * @brief Step 3: Split on the field that creates the shortest epochs.
     *
     */
    vector<update_mechanism> field_epochs;
    for (const update_mechanism &epoch : repetition_epochs) {
        // First, get all fields
        unordered_map<icl::discrete_interval<uint64_t>, vector<int>, interval_hash>
            fields;
        for (int x = 0; x < epoch.size(); ++x) {
            const pm_node *node = dynamic_cast<const pm_node*>(boost::get(pmap, epoch[x]));
            auto f = node->field(instance_type);
            // Don't split consecutive field updates here either
            // if (!fields[f].empty()) {
            if (!fields[f].empty() && !node->field_is_array_type(instance_type)) {
                vertex prior = epoch[fields[f].back()];
                if (epoch[x] - prior == 1) {
                    // Then just replace the index
                    fields[f].back() = x;
                }
            } else {
                fields[f].push_back(x);
            }
        }

        unordered_map<icl::discrete_interval<uint64_t>, size_t, interval_hash>
            max_epoch_lengths;
        for (const auto &p : fields) {
            if (p.second.size() <= 1) {
                continue;
            }

            if (!max_epoch_lengths.count(p.first)) {
                max_epoch_lengths[p.first] = 0;
            }
            // Slice and compute max range
            auto sliced = split_vector(epoch, p.second);
            for (const auto &slice : sliced) {
                size_t r = slice.back() - slice.front();
                max_epoch_lengths[p.first] = std::max(r, max_epoch_lengths.at(p.first));
            }
        }

        // Find minimum of the max ranges.
        vector<int> min_idxs;
        size_t min_range = SIZE_MAX;
        for (const auto &p : max_epoch_lengths) {
            if (p.second < min_range) {
                min_range = p.second;
                min_idxs = fields[p.first];
            }
        }

        // Create the slices.
        auto sliced = split_vector(epoch, min_idxs);
        field_epochs.insert(field_epochs.end(), sliced.begin(), sliced.end());
    }

    BOOST_ASSERT_MSG(field_epochs.size() >= 1, "Have to have at least one epoch!");

    return field_epochs;
}

static size_t edge_count(const graph_type &g, const update_mechanism &um) {
    size_t nedges = 0;
    for (vertex v : um) {
        graph_type::out_edge_iterator it, end;
        for (boost::tie(it, end) = boost::out_edges(v, g); it != end; ++it) {
            if (std::count(um.begin(), um.end(), it->m_target)) nedges++;
        }
    }
    return nedges;
}

// We want fewer edges: means more orderings
static bool update_mechanism_edge_order(const graph_type &g, const update_mechanism &a, const update_mechanism &b) {
    return edge_count(g, a) < edge_count(g, b);
}

static bool update_mechanism_vertex_order(const update_mechanism &a, const update_mechanism &b) {
    return a.size() > b.size();
}

static icl::discrete_interval<vertex> update_mechanism_range(const update_mechanism &u) {
    return icl::interval<vertex>::closed(u.front(), u.back());
}

static bool is_induced_subgraph_in_type(
    const Type *ty,
    const graph_type &g,
    const update_mechanism &large,
    const update_mechanism &small)
{
    // It actually has to come from this one, we didn't create a property
    // map for the fully connected version.
    const_property_map pmap = boost::get(pnode_property_t(), g);

    /**
     * Map small vertices to the large graph.
     *
     * TODO: There may be multiple mappings. But, for now, we should trust our
     * definition of epochs so that the beginnings of the epochs should line up.
     *
     * Otherwise, this problem is quite hard.
     */
    unordered_map<vertex, vertex> s_to_l;
    vector<vertex> large_subset;
    for (const vertex &lv : large) {
        for (const vertex &sv : small) {
            if (s_to_l.count(sv)) continue;

            const pm_node *sn = dynamic_cast<const pm_node*>(boost::get(pmap, sv));
            const pm_node *ln = dynamic_cast<const pm_node*>(boost::get(pmap, lv));
            BOOST_ASSERT(sn && ln);

            if (sn->is_equivalent(ty, *ln)) {
                s_to_l[sv] = lv;
                large_subset.push_back(lv);
                break;
            }
        }
    }

    for (const vertex &sv : small) {
        if (!s_to_l.count(sv)) {
            return false;
        }
    }

    /**
     * Create a set of mapped edges for both the large and small graphs. If the
     * small graph edges are a subset of the large, then it is an induced subgraph.
     * This works because the edges encode the vertices.
     */
    set<pair<vertex, vertex>> small_edges, large_edges;

    /**
     * Have to map small to large
     */
    for (const vertex &v : small) {
        graph_type::out_edge_iterator it, end;
        for (boost::tie(it, end) = boost::out_edges(v, g); it != end; ++it) {
            if (!std::count(small.begin(), small.end(), it->m_target)) continue;

            small_edges.insert(
                make_pair(s_to_l.at(it->m_source), s_to_l.at(it->m_target)));
        }
    }

    for (const vertex &v : large_subset) {
        graph_type::out_edge_iterator it, end;
        for (boost::tie(it, end) = boost::out_edges(v, g); it != end; ++it) {
            if (!std::count(large_subset.begin(), large_subset.end(), it->m_target)) {
                continue;
            }

            large_edges.insert(make_pair(it->m_source, it->m_target));
        }
    }

    /**
     * Now, compare the edges. The edges need to be equal for all vertices in
     * the small graph.
     */
    if (small_edges != large_edges) {
        return false;
    }

    return true;
}

/**
 * Unlike the induced subgraph thing, this is superset of nodes, subset of edges,
 * as that should create subset of ordering constraints => superset of crash states.
 * Cut down on redundancy.
*/
static bool is_representative(
    const Type *ty,
    const graph_type &g,
    const update_mechanism &large,
    const update_mechanism &small)
{
    // It actually has to come from this one, we didn't create a property
    // map for the fully connected version.
    const_property_map pmap = boost::get(pnode_property_t(), g);

    /**
     * Map small vertices to the large graph.
     *
     * TODO: There may be multiple mappings. But, for now, we should trust our
     * definition of epochs so that the beginnings of the epochs should line up.
     *
     * Otherwise, this problem is quite hard.
     */
    unordered_map<vertex, vertex> s_to_l;
    vector<vertex> large_subset;
    for (const vertex &lv : large) {
        for (const vertex &sv : small) {
            if (s_to_l.count(sv)) continue;

            const pm_node *sn = dynamic_cast<const pm_node*>(boost::get(pmap, sv));
            const pm_node *ln = dynamic_cast<const pm_node*>(boost::get(pmap, lv));
            assert(sn);
            assert(ln);

            if (sn->is_equivalent(ty, *ln)) {
                s_to_l[sv] = lv;
                large_subset.push_back(lv);
                break;
            }
        }
    }

    for (const vertex &sv : small) {
        if (!s_to_l.count(sv)) {
            return false;
        }
    }

    /**
     * Create a set of mapped edges for both the large and small graphs. If the
     * small graph edges are a subset of the large, then it is an induced subgraph.
     * This works because the edges encode the vertices.
     */
    set<pair<vertex, vertex>> small_edges, large_edges;

    /**
     * Have to map small to large
     */
    for (const vertex &v : small) {
        graph_type::out_edge_iterator it, end;
        for (boost::tie(it, end) = boost::out_edges(v, g); it != end; ++it) {
            if (!std::count(small.begin(), small.end(), it->m_target)) {
                continue;
            }

            small_edges.insert(
                make_pair(s_to_l.at(it->m_source), s_to_l.at(it->m_target)));
        }
    }

    for (const vertex &v : large_subset) {
        graph_type::out_edge_iterator it, end;
        for (boost::tie(it, end) = boost::out_edges(v, g); it != end; ++it) {
            if (!std::count(large_subset.begin(), large_subset.end(), it->m_target)) {
                continue;
            }

            large_edges.insert(make_pair(it->m_source, it->m_target));
        }
    }

    /**
     * Now, compare the edges. Large needs to have fewer edges.
     */
    set<pair<vertex, vertex>>::const_iterator it = large_edges.begin();
    while (it != large_edges.end()) {
        auto &edge = *it;
        if (small_edges.count(edge) == 0) {
            return false;
        }
        it++;
    }

    return true;
}

vector<update_mechanism_group> engine::group_by_representative_in_type(
    const Type *ty,
    const graph_type &g,
    std::vector<update_mechanism> &mechanisms) const
{
    vector<update_mechanism_group> groups;
    // First, have to sort by number of internal edges
    std::stable_sort(mechanisms.begin(), mechanisms.end(),
        [&] (const update_mechanism &a, const update_mechanism &b) {
             return update_mechanism_edge_order(g, a, b); } );
    // Then, we sort by size, largest to smallest.
    std::stable_sort(mechanisms.begin(), mechanisms.end(),
        update_mechanism_vertex_order);
    cerr << "\tGroup by for " << mechanisms.size() << " update mechanisms\n";

    // If these types are not structure types, then just group on location.
    if (isa<PointerType>(ty) || isa<IntegerType>(ty)) {
        set<vector<stack_frame>> covered_locations;
        const_property_map pmap = boost::get(pnode_property_t(), g);
        size_t skip_covered = 0;
        auto get_stack = [&] (vertex v) {
            const pm_node *p = dynamic_cast<const pm_node*>(boost::get(pmap, v));
            assert(p && "Node is null!");
            return p->event()->stack;
        };

        auto covers = [&] (const update_mechanism &a, const update_mechanism &b) {
            set<vector<stack_frame>> covered_locations;
            for (vertex v : a) {
                covered_locations.insert(get_stack(v));
            }

            for (vertex v : b) {
                if (!covered_locations.count(get_stack(v))) return false;
            }
            return true;
        };

        for (const update_mechanism &m : mechanisms) {
            bool covered = false;
            for (update_mechanism_group &g : groups) {
                if (covers(g.front(), m)) {
                    covered = true;
                    g.push_back(m);
                }
            }
            if (!covered) {
                groups.push_back(update_mechanism_group{m});
            }
        }

        return groups;
    }

    // Now, we add things to groups.
    if (config_enabled("general.use_induced_subgraph")) {
        cerr << "\tWill group with induced subgraph relation\n";
    } else {
        cerr << "\tWill group with representative crash-state relation\n";
    }

    for (int i = 0; i < mechanisms.size(); ++i) {
        const update_mechanism &u = mechanisms[i];
        // cerr << "\t\tUM: " << i << "; NGroups: " << groups.size() << "\n";
        // - See if this belongs to any groups.
        bool belongs = false;
        for (update_mechanism_group &group : groups) {
            bool represents = false;
            if (config_enabled("general.use_induced_subgraph")) {
                represents = is_induced_subgraph_in_type(ty, g, group.front(), u);
            } else {
                represents = is_representative(ty, g, group.front(), u);
            }

            if (represents) {
                group.push_back(u);
                belongs = true;
            }
            // - Keep going; add to all groups
        }
        if (!belongs) {
            update_mechanism_group new_g({u});
            groups.push_back(new_g);
        }
    }

    cerr << "\tDONE Group by for " << mechanisms.size() << " update mechanisms\n";

    return groups;
}

type_to_group_of_um_group engine::get_update_mechanisms_by_type(
    const trace &trace,
    const pm_graph &pg) const
{
    type_to_group_of_um_group umap;

    unordered_map<const Type*, shared_future<vector<update_mechanism_group>>> future_umap;

    const_property_map pmap = boost::get(pnode_property_t(), pg.whole_program_graph());

    // iangneal: HUGE memory sink for larger test cases, duh.
    // https://www.boost.org/doc/libs/1_47_0/libs/graph/doc/transitive_closure.html
    // graph_type fully_connected;
    // boost::transitive_closure(pg.whole_program_graph(), fully_connected);

    cerr << "Number of structs: " <<
        pg.type_crawler().all_types().size() << "\n\n";

    /**
     * There is a boost subgraph class, but I think that maintaining my own
     * "subgraphs" separately will be easier to keep track of.
     */
    for (const Type *ty : pg.type_crawler().all_types()) {
    // for (const Type *ty : pg.type_crawler().all_struct_types()) {
        // STEP 1: Filter by type and instance
        // - Not making an explicit type subgraph here.
        unordered_map<uint64_t, vector<vertex>> instance_vertices;

        graph_type::vertex_iterator it, end;
        for (boost::tie(it, end) = boost::vertices(pg.whole_program_graph()); it != end; ++it) {
            const vertex &v = *it;
            const pm_node *node = dynamic_cast<const pm_node*>(boost::get(pmap, v));
            assert(node && "Node is null!");

            // iangneal: Filter out nodes if we are selectively testing
            if (!trace.within_testing_range(node->event())) {
                continue;
            }

            if (node->is_member_of(ty)) {
                instance_vertices[node->instance_address(ty)].push_back(v);
            }
        }

        // We don't need to make explicit subgraphs, we can just keep lists
        // of nodes.

        llvm::outs() << "Type " << *ty << " has "
            << instance_vertices.size() << " instances!\n";

        assert(umap[ty].empty());

        // STEP 3: Epoch Subgraphs
        vector<update_mechanism> all_epochs;
        for (const auto &p : instance_vertices) {
            uint64_t iaddr = p.first;
            const vector<vertex> &instance = p.second;
            vector<update_mechanism> epochs = split_by_epochs(ty,
                pg.whole_program_graph(), instance);

            all_epochs.insert(all_epochs.end(), epochs.begin(), epochs.end());
        }

        // STEP 4: Group across instances
        if (config_enabled("general.parallelize")) {
            future_umap[ty] = std::async(launch::async,
                [&] (const Type *t, update_mechanism_group g) {
                    return group_by_representative_in_type(t, pg.whole_program_graph(), g);
                }, ty, all_epochs).share();
            // future_umap[ty].wait();
        } else {
            vector<update_mechanism_group> groups = group_by_representative_in_type(
                ty, pg.whole_program_graph(), all_epochs);

            umap[ty].insert(umap[ty].end(), groups.begin(), groups.end());
        }
    }

    if (config_enabled("general.parallelize")) {
        for (auto &p : future_umap) {
            const Type *ty = p.first;
            if (!p.second.valid()) {
                cerr << "Bad grouping state!\n";
                exit(EXIT_FAILURE);
            }
            p.second.wait();
            const vector<update_mechanism_group> &groups = p.second.get();
            umap[ty].insert(umap[ty].end(), groups.begin(), groups.end());
        }
    }

    cout << "(Finished getting update mechanisms!)\n";

    return umap;
}

pair<string, int> engine::get_longest_common_prefix(
    const vector<stack_frame> &a,
    const vector<stack_frame> &b) const
{
    // start from end of vector since the stack frames are in reverse order
    int i = a.size() - 1;
    int j = b.size() - 1;
    // first get the end point of each stack where the file is known
    // this helps us identify only meaningful update protocols
    int end_i = 0;
    int end_j = 0;
    while (end_i < a.size()) {
        if (a[end_i].file != "unknown") {
            break;
        }
        end_i++;
    }
    while (end_j < b.size()) {
        if (b[end_j].file != "unknown") {
            break;
        }
        end_j++;
    }
    // cout << "first string: " << a[end_i].function << endl;
    // cout << "second string: " << a[end_j].function << endl;
    while (i >= end_i && j >= end_j && a[i] == b[j]) {
        i--;
        j--;
    }
    if (i == a.size() - 1) {
        return make_pair("", -1);
    }
    return make_pair(a[i+1].function, a.size() - i - 2);
}

bool engine::is_induced_subgraph_in_function(
    string function,
    const update_mechanism &large,
    const update_mechanism &small) const
{
    // It actually has to come from this one, we didn't create a property
    // map for the fully connected version.
    const graph_type &g = pg_->whole_program_graph();
    const_property_map pmap = boost::get(pnode_property_t(), g);

    /**
     * Map small vertices to the large graph.
     *
     */
    unordered_map<vertex, vertex> s_to_l;
    vector<vertex> large_subset;
    for (const vertex &lv : large) {
        for (const vertex &sv : small) {
            if (s_to_l.count(sv)) continue;

            const posix_node *sn = dynamic_cast<const posix_node*>(boost::get(pmap, sv));
            const posix_node *ln = dynamic_cast<const posix_node*>(boost::get(pmap, lv));
            BOOST_ASSERT(sn && ln);

            if (sn->is_equivalent(function, *ln)) {
                s_to_l[sv] = lv;
                large_subset.push_back(lv);
                break;
            }
        }
    }

    for (const vertex &sv : small) {
        if (!s_to_l.count(sv)) {
            return false;
        }
    }

    /**
     * Create a set of mapped edges for both the large and small graphs. If the
     * small graph edges are a subset of the large, then it is an induced subgraph.
     * This works because the edges encode the vertices.
     */
    set<pair<vertex, vertex>> small_edges, large_edges;

    /**
     * Have to map small to large
     */
    for (const vertex &v : small) {
        graph_type::out_edge_iterator it, end;
        for (boost::tie(it, end) = boost::out_edges(v, g); it != end; ++it) {
            if (!std::count(small.begin(), small.end(), it->m_target)) continue;

            small_edges.insert(
                make_pair(s_to_l.at(it->m_source), s_to_l.at(it->m_target)));
        }
    }

    for (const vertex &v : large_subset) {
        graph_type::out_edge_iterator it, end;
        for (boost::tie(it, end) = boost::out_edges(v, g); it != end; ++it) {
            if (!std::count(large_subset.begin(), large_subset.end(), it->m_target)) {
                continue;
            }

            large_edges.insert(make_pair(it->m_source, it->m_target));
        }
    }

    /**
     * Now, compare the edges. The edges need to be equal for all vertices in
     * the small graph.
     */
    if (small_edges != large_edges) {
        return false;
    }

    return true;
}

vector<update_mechanism_group> engine::group_by_representative_in_function(
    string function,
    vector<update_mechanism> &mechanisms) const {
    
    vector<update_mechanism_group> groups;
    // sort by size first
    std::stable_sort(mechanisms.begin(), mechanisms.end(),
        update_mechanism_vertex_order);
    // now iterate over all mechanisms, check if it is represented by any group representative (first of the group), if not, create a new group with the mechanism
    for (int i = 0; i < mechanisms.size(); ++i) {
        const update_mechanism &u = mechanisms[i];
        bool belongs = false;
        for (update_mechanism_group &group : groups) {
            if (is_induced_subgraph_in_function(function, group.front(), u)) {
                group.push_back(u);
                belongs = true;
            }
        }
        if (!belongs) {
            update_mechanism_group new_g({u});
            groups.push_back(new_g);
        }
    }
    return groups;
}

update_mechanism_group engine::group_by_parent_child(tree<shared_ptr<stack_tree_node>>::iterator it, stack_tree* st) const {
    update_mechanism_group new_group;
    update_mechanism aggregate_um;
    // nothing to group
    if (st->root() == it) {
        return new_group;
    }
    shared_ptr<stack_tree_node> node = *it;
    // if leaf node, directly return the function and the update mechanism group
    if (st->is_leaf(it)) {
        return node->um_group;
    }

    // aggregate current node's update mechanisms
    for (const auto& um : node->um_group) {
        aggregate_um.insert(aggregate_um.end(), um.begin(), um.end());
    }

    // get children update mechanisms
    auto children_its = st->children(it);
    // for debug
    for (auto child_it : children_its) {
        // shared_ptr<stack_tree_node> child_node = *child_it;
        // update_mechanism_group child_um_group = child_node->um_group;
        // for (const auto& ums : child_um_group) {
        //     aggregate_um.insert(aggregate_um.end(), ums.begin(), ums.end());
        // }
        // now recursive to get the children's children
        update_mechanism_group child_group = group_by_parent_child(child_it, st);
        for (const auto& um : child_group) {
            aggregate_um.insert(aggregate_um.end(), um.begin(), um.end());
        }
    }

    // std::sort(aggregate_um.begin(), aggregate_um.end());
    new_group = split_by_clustering(aggregate_um);
    return new_group;
}

// update_mechanism_group engine::split_contiguous(const update_mechanism& um) const {
//     update_mechanism_group splits;
//     if (um.empty()) {
//         return splits;
//     }

//     update_mechanism current_seq;
//     current_seq.push_back(um[0]);

//     for (size_t i = 1; i < um.size(); ++i) {
//         if (um[i] == um[i - 1] + 1) {
//             current_seq.push_back(um[i]);
//         } else {
//             splits.push_back(current_seq);
//             current_seq.clear();
//             current_seq.push_back(um[i]); 
//         }
//     }
//     splits.push_back(current_seq);

//     return splits;
// }

update_mechanism_group engine::split_by_clustering(const update_mechanism& um) const {
    unordered_map<uint64_t, vertex> event_id_to_vertex;
    const_property_map pmap = boost::get(pnode_property_t(), pg_->whole_program_graph());

    arma::mat data(1, um.size());
    for (int i = 0; i < um.size(); ++i) {
        vertex v = um[i];
        const posix_node *node = dynamic_cast<const posix_node*>(get(pmap, v));
        event_id_to_vertex[node->event()->event_idx()] = v;
        data(0, i) = node->event()->event_idx();
    }
    

    // Parameters for DBSCAN.
    double epsilon = 10.0;
    size_t min_points = 1;

    // Output: assignments of each point to a cluster and the number of clusters.
    arma::Row<size_t> assignments;
    arma::mat centroids;

    // Run DBSCAN clustering.
    mlpack::dbscan::DBSCAN<> dbscan(epsilon, min_points);
    dbscan.Cluster(data, assignments, centroids);

    // Print the assignments.
    // std::cout << "Cluster assignments: " << assignments << std::endl;

    unordered_map<size_t, vector<uint64_t>> cluster_to_um;
    for (size_t i = 0; i < assignments.n_elem; ++i) {
        size_t cluster = assignments[i];
        cluster_to_um[cluster].push_back(data(0, i));
        // cout << "Cluster " << cluster << " has event id " << data(0, i) << endl;
    }

    update_mechanism_group splits;
    // translate event id splits back to vertex splits
    for (const auto & p : cluster_to_um) {
        update_mechanism split;
        for (uint64_t event_id : p.second) {
            split.push_back(event_id_to_vertex[event_id]);
        }
        splits.push_back(split);
    }
    

    return splits;
}

update_mechanism engine::generate_continuous_update_mechanism(const update_mechanism &um) const {
    // get event ids
    uint64_t min_event_id = UINT64_MAX;
    uint64_t max_event_id = 0;
    const trace &trace = pg_->get_trace();
    for (vertex v : um) {
        const posix_node *node = dynamic_cast<const posix_node*>(get(pnode_property_t(), pg_->whole_program_graph(), v));
        uint64_t event_id = node->event()->event_idx();
        if (event_id < min_event_id) {
            min_event_id = event_id;
        }
        if (event_id > max_event_id) {
            max_event_id = event_id;
        }
    }

    // here we add a few margin events before and after the update mechanism
    min_event_id = (min_event_id > 3) ? min_event_id - 3 : 0;
    max_event_id = (max_event_id < trace.events().size() - 4) ? max_event_id + 3 : trace.events().size() - 1;

    update_mechanism continuous_um;
    for (uint64_t i = min_event_id; i <= max_event_id; ++i) {
        if (trace.events()[i]->is_marker_event()) {
            continue;
        }
        continuous_um.push_back(pg_->get_vertex(trace.events()[i]));
    }
    return continuous_um;
}


function_to_group_of_um_group engine::get_update_mechanisms_by_function(
    const trace &trace,
    const posix_graph &pg) const {
    function_to_group_of_um_group fmap;
    unordered_map<string, update_mechanism_group> function_to_um_group;
    /*
        This will be the core rountine to derive update protocols based on backtraces.
        The key insight here is to use the source code line change between adjacent events to infer which update protocol is under execution.
        The algorithm works as follows:
            1. Split the trace by thread id
            2. For each thread, traverse through the events
                For events[i] and events[i+1], 
                a. If events[i] is not currently in an update protocol, check the longest prefix against events[i+1], create a new update protocol p = {i, i+1}
                b. If events[i] is currently in an update protocol, check the longest prefix against events[i+1]
                    i. If the longest prefix is the same as the current update protocol, add i+1 to p
                    ii. If the longest prefix is different
                        1. If the prefix is a parent function, end current protocol, continue
                        2. If the prefix is a child function, remove i from current protocol p, create a new protocol p_new = {i, i+1}
        
        In the meantime, we maintain a function call tree for potential grouping and splitting of update protocols. Each node in the tree represents a function and the parent-child relationship is determined by the call stack. Each node will have a pointer to a list of update protocols.
    */
    
    unordered_map<uint64_t, stack_tree*> thread_to_stack_tree;
    unordered_map<uint64_t, vector<shared_ptr<trace_event>>> thread_to_events;

    fs::path update_protocol_log = fs::path(output_dir_) / "update_protocol.log";
    ofstream log_file(update_protocol_log);

    // first split the trace by thread id
    for (const shared_ptr<trace_event> &event : trace.events()) {
        if (event->is_marker_event()) continue;
        thread_to_events[event->thread_id()].push_back(event);
        thread_to_stack_tree[event->thread_id()] = new stack_tree("SHADOW_ROOT", pg, output_dir_);
    }
    // now for each thread, we traverse through the events
    for (const auto &p : thread_to_events) {
        uint64_t tid = p.first;
        const vector<shared_ptr<trace_event>> &events = p.second;
        if (events.size() < 2) {
            std::cerr << "Warning: not enough events for update protocol inference, skipped thread " << tid << std::endl;
            continue;
        }
        // for two adjacent events, check the backtrace to see which stack frame differs in line number
        update_mechanism um;
        string current_protocol;
        bool in_update_protocol = false;
        pair<string, int> prefix;
        stack_tree* st = thread_to_stack_tree[tid];
        tree<shared_ptr<stack_tree_node>>::iterator root_it = st->root();
        const_property_map pmap = boost::get(pnode_property_t(), pg.whole_program_graph());
        for (int i = 0; i < events.size() - 1; ++i) {
            const shared_ptr<trace_event> &left_event = events[i];
            const shared_ptr<trace_event> &right_event = events[i+1];
            // first update the call tree, and get the leaf nodes for the end of stack frames (excluding intrinsic functions)
            tree<shared_ptr<stack_tree_node>>::iterator left_it = st->process_backtrace(left_event->backtrace());
            tree<shared_ptr<stack_tree_node>>::iterator right_it = st->process_backtrace(right_event->backtrace());
            // then check the longest common prefix
            pair<string, int> new_prefix = get_longest_common_prefix(left_event->backtrace(), right_event->backtrace());
            if (!in_update_protocol) {
                if (new_prefix.second == -1) {
                    cout << "!in_update_protocol" << endl;
                    cerr << "Error: no common prefix between two adjacent events!" << endl;
                    exit(EXIT_FAILURE);
                }
                // we don't choose the "one-off" of longest prefix to be the update protocol when
                // 1. the longest prefix is the last frame of the backtrace, or
                // 2. the next stack frame after the longest prefix is an intrinsic function
                // 3. the next stack frame's two functions are different
                else if (new_prefix.second == left_event->backtrace().size() - 1 || left_event->backtrace()[left_event->backtrace().size()-1 - new_prefix.second-1].file == "unknown" || left_event->backtrace()[left_event->backtrace().size()-1 - new_prefix.second-1].function != right_event->backtrace()[right_event->backtrace().size()-1 - new_prefix.second-1].function) {
                    current_protocol = new_prefix.first;
                }
                else {
                    current_protocol = left_event->backtrace()[left_event->backtrace().size()-1 - new_prefix.second-1].function;
                }
                in_update_protocol = true;
                prefix = new_prefix;
                // TODO: change to vertex idx
                um.push_back(pg.get_vertex(left_event));
                um.push_back(pg.get_vertex(right_event));
            }
            else {
                if (new_prefix.second == -1) {
                    cerr << "Error: no common prefix between two adjacent events!";
                    exit(EXIT_FAILURE);
                }
                else if (new_prefix.second > prefix.second) {
                    // if longest common prefix is deeper, this is an indication of deeper new function called, we should pop the left event and end current protocol
                    um.pop_back();
                    log_file << "Current protocol" << current_protocol << endl;
                    for (int i = 0; i < um.size(); ++i) {
                        log_file << um[i] << " ";
                    }
                    log_file << endl;
                    // // prepare to add to fmap, we add a margin of 1 event after the last event in the update protocol, to help edge case that the last event may be a sync family event
                    // if (i+2 < events.size()) {
                    //     um.push_back(pg.get_vertex(events[i+2]));
                    // }
                    // add to fmap
                    function_to_um_group[current_protocol].push_back(um);
                    bool found = false;
                    while (left_it != root_it) {
                        shared_ptr<stack_tree_node> left_node = *left_it;
                        if (left_node->function == current_protocol) {
                            left_node->um_group.push_back(um);
                            found = true;
                            break;
                        }
                        left_it = st->parent(left_it);
                        // cout << "left_it" << left_node->function << endl;
                    }
                    assert(found);
                    um = update_mechanism();
                    um.push_back(pg.get_vertex(left_event));
                    um.push_back(pg.get_vertex(right_event));
                    if (new_prefix.second == left_event->backtrace().size() - 1 || left_event->backtrace()[left_event->backtrace().size()-1 - new_prefix.second-1].file == "unknown") {
                        current_protocol = left_event->backtrace()[left_event->backtrace().size()-1 - new_prefix.second].function;
                    }
                    else {
                        current_protocol = left_event->backtrace()[left_event->backtrace().size()-1 - new_prefix.second-1].function;
                    }
                    prefix = new_prefix;
                }
                else if (new_prefix.second == prefix.second) {
                    um.push_back(pg.get_vertex(right_event));
                    // to prevent a very long loop or calling same function all the time, we should have some way to restrict the size of the protocol
                    // TODO: currently I just select a magic number that is feasible for model checking, but whether this is the best way is debatable
                    if (um.size() >= max_um_size_) {
                        log_file << "Current protocol" << current_protocol << endl;
                        for (int i = 0; i < um.size(); ++i) {
                            log_file << um[i] << " ";
                        }
                        log_file << endl;
                        // // prepare to add to fmap, we add a margin of 1 event after the last event in the update protocol, to help edge case that the last event may be a sync family event
                        // if (i+2 < events.size()) {
                        //     um.push_back(pg.get_vertex(events[i+2]));
                        // }
                        // add to fmap
                        function_to_um_group[current_protocol].push_back(um);
                        bool found = false;
                        while (left_it != root_it) {
                            shared_ptr<stack_tree_node> left_node = *left_it;
                            if (left_node->function == current_protocol) {
                                left_node->um_group.push_back(um);
                                found = true;
                                break;
                            }
                            left_it = st->parent(left_it);
                        }
                        assert(found);
                        um = update_mechanism();
                        in_update_protocol = false;
                    
                    }
                }
                else {
                    // if the longest common prefix is shorter than the last prefix, this is an indication of current protocol as just ended, we should end the current protocol
                    log_file << "Current protocol" << current_protocol << endl;
                    for (int i = 0; i < um.size(); ++i) {
                        log_file << um[i] << " ";
                    }
                    log_file << endl;
                    // // prepare to add to fmap, we add a margin of 1 event after the last event in the update protocol, to help edge case that the last event may be a sync family event
                    // if (i+2 < events.size()) {
                    //     um.push_back(pg.get_vertex(events[i+2]));
                    // }
                    // add to fmap
                    function_to_um_group[current_protocol].push_back(um);
                    bool found = false;
                    while (left_it != root_it) {
                        shared_ptr<stack_tree_node> left_node = *left_it;
                        if (left_node->function == current_protocol) {
                            left_node->um_group.push_back(um);
                            found = true;
                            break;
                        }
                        left_it = st->parent(left_it);
                    }
                    assert(found);
                    um = update_mechanism();
                    in_update_protocol = false;
                }
            }
        }
        if (um.size() > 0 ) {
            tree<shared_ptr<stack_tree_node>>::iterator left_it = st->process_backtrace(events[events.size()-2]->backtrace());
            log_file << "Current protocol" << current_protocol << endl;
            for (int i = 0; i < um.size(); ++i) {
                log_file << um[i] << " ";
            }
            log_file << endl;
            // add to fmap
            function_to_um_group[current_protocol].push_back(um);
            bool found = false;
            while (left_it != root_it) {
                shared_ptr<stack_tree_node> left_node = *left_it;
                if (left_node->function == current_protocol) {
                    left_node->um_group.push_back(um);
                    found = true;
                    break;
                }
                left_it = st->parent(left_it);
            }
            assert(found);
        }
        // st->print();
        st->compact();
        stringstream ss;
        ss << "stack_tree_with_protocol_" << tid << ".log";
        st->print(ss.str(), true);
        ss.str("");
        ss << "stack_tree_" << tid << ".log";
        st->print(ss.str(), false);
        // for (auto it = function_to_um_group.begin(); it != function_to_um_group.end(); ++it) {
        //     cout << "Function: " << it->first << endl;
        //     for (int i = 0; i < it->second.size(); ++i) {
        //         cout << "Update protocol " << i << endl;
        //         for (int j = 0; j < it->second[i].size(); ++j) {
        //             // get source code line first
        //             const posix_node *node = dynamic_cast<const posix_node*>(boost::get(pmap, it->second[i][j]));
        //             shared_ptr<trace_event> event = node->event();
        //             int line = -1;
        //             string file;
        //             for (int k = 0; k < event->backtrace().size(); ++k) {
        //                 if (event->backtrace()[k].function == it->first) {
        //                     line = event->backtrace()[k].line;
        //                     file = event->backtrace()[k].file;
        //                     break;
        //                 }
        //             }
        //             assert(line != -1);
        //             cout << "vertex id:" << it->second[i][j] << "|event id:" << event->event_idx() << " -- " << file << " " << line << endl;
        //         }
        //         cout << endl;
        //     }
        // }
    }


    // now we start to generate test ranges, we start by only considering the leaf nodes in the stack tree, and we group the update protocols by the function they belong to

    // parent-child grouping
    function_to_um_group.clear();
    // update height first
    for (const auto &p : thread_to_stack_tree) {
        p.second->update_height();
    }
    for (int height = 0; height < MAX_HEIGHT; ++height) {
        for (const auto &p : thread_to_stack_tree) {
            if (thread_to_events[p.first].size() < 2) {
                // skip trivial threads
                continue;
            }
            vector<tree<shared_ptr<stack_tree_node>>::iterator> leaf_nodes = p.second->get_nodes_at_height(height);
            for (const auto &it : leaf_nodes) {
                update_mechanism_group new_group = group_by_parent_child(it, p.second);
                shared_ptr<stack_tree_node> cur_node = *it;
                if (new_group.size() > 0) {
                    function_to_um_group[cur_node->function].insert(function_to_um_group[cur_node->function].end(), new_group.begin(), new_group.end());
                }
            
            }
        }
    }

    // original
    // get all update protocols at leaf nodes
    // function_to_um_group.clear();
    // for (const auto &p : thread_to_stack_tree) {
    //     vector<tree<shared_ptr<stack_tree_node>>::iterator> leaf_nodes = p.second->leaves();
    //     for (const auto &it : leaf_nodes) {
    //         shared_ptr<stack_tree_node> node = *it;
    //         if (node->um_group.size() > 0) {
    //             function_to_um_group[node->function].insert(function_to_um_group[node->function].end(), node->um_group.begin(), node->um_group.end());
    //         }
    //     }
    // }
    // second pass, iterate over all update protocols, split gapped update protocol by clustering
    for (auto &p : function_to_um_group) {
        update_mechanism_group new_group;
        for (auto &um : p.second) {
            update_mechanism_group split = split_by_clustering(um);
            new_group.insert(new_group.end(), split.begin(), split.end());
        }
        p.second = new_group;
    }
    
    for (auto &p : function_to_um_group) {
        fmap[p.first] = group_by_representative_in_function(p.first, p.second);
    }

    // print fmap for debugging
    log_file << "### Final update protocols after grouping ###\n";
    for (const auto &p : fmap) {
        log_file << "Function: " << p.first << endl;
        for (int i = 0; i < p.second.size(); ++i) {
            log_file << "Group " << i << endl;
            for (int j = 0; j < p.second[i].size(); ++j) {
                for (int k = 0; k < p.second[i][j].size(); ++k) {
                    log_file << p.second[i][j][k] << " ";
                }
                log_file << endl;
            }
        }
    }

    // clean up stack trees
    for (const auto &p : thread_to_stack_tree) {
        delete p.second;
    }

    return fmap;
}

void engine::run_representative_testing_by_function(const trace &t, function_to_group_of_um_group &fmap) {
    // we know that for fmap, in each group, the first is representative graph
    // our goal is to extract this front graph, generate all possible orders, feed it to model checker
    model_checker checker(t, output_dir_, chrono::seconds(config_int("test.timeout")), pg_, PATHFINDER, mode_, op_tracing_, persevere_);
    int test_idx = 0;
    int global_instance_idx = 0;
    // store all inconsistent <global_id, test_id> pairs
    vector<tuple<int, int>> inconsistent_instances;
    // store crash state info, tuple of <crash state tested, total crash states>
    vector<tuple<int, tuple<int, int>>> crash_state_info;
    // global instance idx to (function and test_idx, instance idx)
    unordered_map<int, tuple<string, int, int>> global_idx_to_function_and_idx;
    // global instance idx to (model_checker_state and result code)
    unordered_map<int, pair<shared_ptr<model_checker_state>, shared_future<model_checker_code>>> global_idx_to_state_and_res;

    // for outputting information
    fs::path info_file = output_dir_ / "info.txt";
    ofstream info(info_file.string());
    bio::tee_device<ostream, ofstream> tout_dev(cout, info);
    bio::stream<bio::tee_device<ostream, ofstream>> tout(tout_dev);

    for (auto &p : fmap) {
        string function = p.first;
        vector<update_mechanism_group> &groups = p.second;
        tout << "Test " << test_idx << " Testing on function: " << function << endl;
        // if (function.find("ProcessManifestWrites") == string::npos) {
        //     test_idx++;
        //     continue;
        // }
        int instance_idx = 0;
        for (auto &group : groups) {
            update_mechanism representative = group.front();
            const_property_map pmap = boost::get(pnode_property_t(), pg_->whole_program_graph());
            tout << "vertex:event ";
            // check if in the representative, all the events have no micro events
            // If so, that (probably) means this test is meaningless and we should skip (i.e. contains only open/close calls)
            bool valueable = false;
            for (auto &vertex : representative) {
                const posix_node *node = dynamic_cast<const posix_node*>(boost::get(pmap, vertex));
                tout << vertex << ":" << node->event()->event_idx() << " ";
                assert(node->event()->micro_events);
                if (node->event()->micro_events->size() > 0) {
                    valueable = true;
                }
            }
            tout << endl;

            if (!valueable) {
                tout << "Test " << test_idx << " has no useful updates, skip" << endl;
                test_idx++;
                continue;
            }

            posix_graph *pg_ptr = dynamic_cast<posix_graph*>(pg_);

            tout << "Test " << test_idx << " representativeness " << "1/" << group.size() << endl;
            
            // shared_ptr<model_checker_state> test = create_test(checker, *pg_ptr, representative);
            // shared_future<model_checker_code> res = checker.run_test(test);
            // res.wait();
            // auto code = res.get();
            // if (has_bugs(code)) {
            //     cout << "POSIX test "<< test_idx <<" instance " << instance_idx <<" is crash-inconsistent!" << endl;
            // } else {
            //     cout << "POSIX test "<< test_idx <<" instance " << instance_idx <<" is crash-consistent!" << endl;
            // }
            // instance_idx++;

            //TODO: currently file state restore has some issues, we will create one state for each ordering
            vector<vector<vector<int>>> vec_of_all_event_orders;

            // for Persevere, we need to keep track of all representative update mechanisms that will be testd
            vector<update_mechanism> vec_of_all_update_mechanisms_tested;

            // crash state info
            tuple<int, int> crash_state_info_instance(0, 0);
            // any preprocessing needed for the representative
            // split_by_clustering(representative);
            representative = generate_continuous_update_mechanism(representative);
            if (representative.size() > max_um_size_){
                if (representative.size() > LINEAR_MAX_SIZE) {
                    tout << "Too many events in function " << function << " test "<< test_idx << " skip for now" << endl;
                    test_idx++;
                    continue;
                }
                // just do linear order for now if the size is too big
                // TODO: include this in vec_of_all_update_mechanisms_tested?
                vector<vector<int>> all_event_orders;
                vector<int> event_order;
                for (auto &v : representative) {
                    const posix_node *node = dynamic_cast<const posix_node*>(boost::get(pmap, v));
                    event_order.push_back(node->event()->event_idx());
                    all_event_orders.push_back(event_order);
                }

                // if (config_enabled("general.count_crash_state")) {
                //     // the order is too big to count for crash info, we approximate total crash state by the crash states we tested times the group size, note that this is actually in favor of exhaustive testing
                //     get<0>(crash_state_info_instance) = all_event_orders.size();
                //     get<1>(crash_state_info_instance) = all_event_orders.size() * group.size();
                // }
                vec_of_all_event_orders.push_back(all_event_orders);

                vec_of_all_update_mechanisms_tested.push_back(representative);
            }
            else {
                vector<set<set<vertex>>> vec_of_all_vertex_orders;
                // set<set<vertex>> all_vertex_orders = pg_ptr->generate_all_orders(representative);
                atomic<bool> cancel_flag(false);

                auto future = async(launch::async, &persistence_graph::generate_all_orders, pg_ptr, representative, std::ref(cancel_flag));

                // set<set<vertex>> all_vertex_orders;
                int timeout_seconds = 5;
        

                if (future.wait_for(chrono::seconds(timeout_seconds)) == future_status::ready) {
                    // Function completed within the timeout
                    vec_of_all_update_mechanisms_tested.push_back(representative);

                    set<set<vertex>> all_vertex_orders = future.get();
                    vec_of_all_vertex_orders.push_back(all_vertex_orders);
                    cout << "Function completed within the timeout." << endl;

                    // if (config_enabled("general.count_crash_state")) {
                    //     // crash state info
                    //     get<0>(crash_state_info_instance) = all_vertex_orders.size();

                    //     cancel_flag.store(false);

                    //     // Launch an async task to compute crash states
                    //     auto future_total = std::async(std::launch::async, [&]() {
                    //         int sum = 0;
                    //         for (auto &um : group) {
                    //             // Check if we were cancelled
                    //             if (cancel_flag.load()) {
                    //                 break;
                    //             }
                    //             // Possibly update the "continuous update mechanism" first
                    //             um = generate_continuous_update_mechanism(um);

                    //             // compute the crash states for this update mechanism
                    //             auto crash_states = pg_ptr->generate_all_orders(um, cancel_flag);
                    //             sum += static_cast<int>(crash_states.size());
                    //         }
                    //         return sum;
                    //     });

                    //     int total_crash_states = 0;
                    //     // Wait up to 1 minute for the result
                    //     if (future_total.wait_for(std::chrono::minutes(1)) == std::future_status::timeout) {
                    //         // Timed out -> set cancel_flag so the async task can stop
                    //         cancel_flag.store(true);
                    //         // Timed out: set result to representative size times group size
                    //         cout << "Crash state calculation timed out, use representative size times group size as an approximation." << endl;
                    //         total_crash_states = all_vertex_orders.size() * group.size();
                    //     } else {
                    //         // Completed within 1 minute
                    //         total_crash_states = future_total.get();
                    //     }

                    //     get<1>(crash_state_info_instance) = total_crash_states;
                    // }

                } else {
                    // Timeout occurred
                    cout << "Function timed out." << endl;
                    // Optionally handle the timeout case here
                    cancel_flag.store(true);
                    future.wait();
                    // skip this test
                    // test_idx++;
                    // TODO: if timed out, chunk this region into smaller pieces, potentially overlapping
                    if (representative.size() >= UM_CHUNK_SIZE) {
                        cancel_flag.store(false);
                        for (int i = 0; i < representative.size(); i += UM_CHUNK_GAP) {
                            int chunk_size = std::min(UM_CHUNK_SIZE, (int)representative.size() - i);
                            update_mechanism chunk(representative.begin() + i, representative.begin() + i + chunk_size);
                            set<set<vertex>> chunk_vertex_orders = pg_ptr->generate_all_orders(chunk, cancel_flag);
                            vec_of_all_vertex_orders.push_back(chunk_vertex_orders);
                            vec_of_all_update_mechanisms_tested.push_back(chunk);
                        }
                        // if (config_enabled("general.count_crash_state")) {
                        //     // crash state info
                        //     int representative_crash_states = 0;
                        //     for (auto & avo : vec_of_all_vertex_orders) {
                        //         representative_crash_states += avo.size();
                        //     }
                        //     // again, we cannot count the total crash states, so we approximate it by the crash states we tested times the group size
                        //     get<0>(crash_state_info_instance) = representative_crash_states;
                        //     get<1>(crash_state_info_instance) = representative_crash_states * group.size();
                        // }
                    } else {
                        tout << "Function " << function << " test "<< test_idx << " is too large, skip for now" << endl;
                        test_idx++;
                        continue;
                    }
                    // tout << "Function " << function << " test "<< test_idx << " is too large, skip for now" << endl;
                    // test_idx++;
                    // continue;
                }
                for (auto &all_vertex_orders : vec_of_all_vertex_orders) {
                    vector<vector<int>> all_event_orders;
                    for (auto &order : all_vertex_orders) {
                        vector<int> event_order;
                        for (auto &v : order) {
                            const posix_node *node = dynamic_cast<const posix_node*>(boost::get(pmap, v));
                            event_order.push_back(node->event()->event_idx());
                        }
                        // sort by event index
                        std::sort(event_order.begin(), event_order.end());
                        all_event_orders.push_back(event_order);
                    }
                    vec_of_all_event_orders.push_back(all_event_orders);
                }
            }

            if (config_enabled("general.persevere") || config_enabled("general.count_crash_state")) {
                int representative_crash_states = 0;
                for (auto & um : vec_of_all_update_mechanisms_tested) {
                    if (um.size() > max_um_size_) {
                        // we use linear test in this case
                        representative_crash_states += um.size();
                        continue;
                    }
                    vector<int> event_order;
                    for (auto &v : um) {
                        const posix_node *node = dynamic_cast<const posix_node*>(boost::get(pmap, v));
                        event_order.push_back(node->event()->event_idx());
                    }
                    // sort by event index
                    std::sort(event_order.begin(), event_order.end());
                    int min_idx = INT_MAX;
                    for (const auto &idx : event_order) {
                            min_idx = std::min(min_idx, idx);
                    }
                    if (min_idx == INT_MAX) {
                        tout << "Invalid set of all_event_orders in min idx calculation " << " skip for now" << endl;
                        continue;
                    }
                    shared_ptr<model_checker_state> test = create_test(checker, event_order, min_idx);
                    fs::path new_root_dir = test->pmdir;
                    fs::path old_root_dir = t.get_root_dir();
                    shared_future<model_checker_code> res = checker.run_test(test);
                    res.wait();
                    auto code = res.get();
                    int checker_test_id = checker.get_current_test_id();
                    assert (checker_test_id >= 0);
                    fs::path read_trace_path = output_dir_ / (std::to_string(checker_test_id) + "_0_0_pinout");
                    assert (fs::exists(read_trace_path));
                    // read the trace
                    trace read_trace(config_enabled("general.selective_testing"), mode_);
                    read_trace.read_offline_trace(read_trace_path);
                    vector<string> read_paths;
                    for (const auto &e : read_trace.events()) {
                        if (e->file_path != "") {
                            // replace the new_root_dir with old_root_dir in the file path
                            string processed_file_path = e->file_path;
                            // set the part of the path that is the same as the new_root_dir to empty
                            processed_file_path.replace(0, new_root_dir.string().size(), "");
                            // add the old_root_dir to the beginning of the path
                            processed_file_path = (old_root_dir / processed_file_path).string();
                            read_paths.push_back(processed_file_path);
                            // cout << processed_file_path << endl;
                        }
                    }
                    update_mechanism new_um;
                    for (auto &v : um) {
                        const posix_node *node = dynamic_cast<const posix_node*>(boost::get(pmap, v));
                        const shared_ptr<trace_event> &event = node->event();
                        if (event->file_path != "") {
                            // string file_name = event->file_path.substr(event->file_path.find_last_of("/")+1);
                            if (std::find(read_paths.begin(), read_paths.end(), event->file_path) != read_paths.end()) {
                                new_um.push_back(v);
                            }
                        } else if (event->new_path != "") {
                            if (std::find(read_paths.begin(), read_paths.end(), event->new_path) != read_paths.end()) {
                                new_um.push_back(v);
                            }
                        } else {
                            new_um.push_back(v);
                        }
                    }
                    atomic<bool> cancel_flag(false);
                    set<set<vertex>> all_vertex_orders = pg_ptr->generate_all_orders(new_um, cancel_flag);
                    representative_crash_states += all_vertex_orders.size();
                }

 
                get<0>(crash_state_info_instance) = representative_crash_states;
                get<1>(crash_state_info_instance) = representative_crash_states * group.size();

                tout << "Crash state info [left:Pathfinder, right:Persevere]: " << endl;
                int total_crash_states_tested = 0;
                int total_crash_states = 0;
                for (auto &p : crash_state_info) {
                    tout << "Test id: " << get<0>(p) << " Crash states tested by Pathfinder: " << get<0>(get<1>(p)) << ", Crash states tested by Persevere: " << get<1>(get<1>(p)) << endl;
                    total_crash_states_tested += get<0>(get<1>(p));
                    total_crash_states += get<1>(get<1>(p));
                }
                tout << "Total crash states tested by Pathfinder: " << total_crash_states_tested << " Total crash states tested by Persevere: " << total_crash_states << endl;
            }
            crash_state_info.push_back(make_tuple(test_idx, crash_state_info_instance));

            // do the actual testing if not counting crash state
            if (!config_enabled("general.count_crash_state") && !config_enabled("general.persevere")) {
                for (auto &all_event_orders : vec_of_all_event_orders) {
                    // get the min index from set<set<vertex>> all_event_orders, for setup point
                    int min_idx = INT_MAX;
                    for (const auto &order : all_event_orders) {
                        for (const auto &idx : order) {
                            min_idx = std::min(min_idx, idx);
                        }
                    }

                    // TODO: if this is invalid set of all_event_orders, skip it
                    // we may produce invalid orders since we do not split by timestamp
                    if (min_idx == INT_MAX) {
                        tout << "Invalid set of all_event_orders in function " << function << " test "<< test_idx << " skip for now" << endl;
                        test_idx++;
                        continue;
                    }

                    // if function contains RenameFile skip for now
                    // if (function.find("RenameFile") != string::npos || function.find("Close") != string::npos) {
                    //     tout << "Skip RenameFile & Close function " << function << " test "<< test_idx << endl;
                    //     test_idx++;
                    //     continue;
                    // }
                    // if (function.find("SetCurrentFile") == string::npos) {
                    //     test_idx++;
                    //     continue;
                    // }

                    for (auto &event_order : all_event_orders) {
                        shared_ptr<model_checker_state> test = create_test(checker, event_order, min_idx);
                        if (!config_enabled("general.parallelize")) {
                            shared_future<model_checker_code> res = checker.run_test(test);
                            res.wait();
                            auto code = res.get();
                            if (has_bugs(code)) {
                                tout << global_instance_idx << "| Function " << function << " test "<< test_idx <<" instance " << instance_idx <<" is crash-inconsistent!" << endl;
                                inconsistent_instances.push_back(make_tuple(global_instance_idx, test_idx));
                            } else {
                                tout << global_instance_idx << "| Function " << function << " test "<< test_idx <<" instance " << instance_idx <<" is crash-consistent!" << endl;
                            }
                        }
                        else {
                            global_idx_to_function_and_idx[global_instance_idx] = make_tuple(function, test_idx, instance_idx);
                            shared_future<model_checker_code> res = checker.run_test(test);
                            global_idx_to_state_and_res[global_instance_idx] = make_pair(test, res);
                            while (global_idx_to_state_and_res.size() > max_nproc_) {
                                vector<int> ids;
                                for (auto &p : global_idx_to_state_and_res) {
                                    ids.push_back(p.first);
                                }
                                for (int id : ids) {
                                    auto &p = global_idx_to_state_and_res[id];
                                    if (p.second.wait_for(chrono::milliseconds(POLL_MILLIS)) == future_status::ready) {
                                        auto code = p.second.get();
                                        auto &function_and_idx = global_idx_to_function_and_idx[id];
                                        string function = get<0>(function_and_idx);
                                        int test_idx = get<1>(function_and_idx);
                                        int instance_idx = get<2>(function_and_idx);
                                        if (has_bugs(code)) {
                                            tout << id << "| Function " << function << " test "<< test_idx <<" instance " << instance_idx <<" is crash-inconsistent!" << endl;
                                            inconsistent_instances.push_back(make_tuple(id, test_idx));
                                        } else {
                                            tout << id << "| Function " << function << " test "<< test_idx <<" instance " << instance_idx <<" is crash-consistent!" << endl;
                                        }
                                        global_idx_to_state_and_res.erase(id);
                                    }
                                }
                            }
                        }
                        instance_idx++;
                        global_instance_idx++;
                    }
                }
            }
            test_idx++;
        }
    }
    if (config_enabled("general.parallelize")) {
        while (global_idx_to_state_and_res.size() > 0) {
            vector<int> ids;
            for (auto &p : global_idx_to_state_and_res) {
                ids.push_back(p.first);
            }
            for (int id : ids) {
                auto &p = global_idx_to_state_and_res[id];
                if (p.second.wait_for(chrono::milliseconds(POLL_MILLIS)) == future_status::ready) {
                    auto code = p.second.get();
                    auto &function_and_idx = global_idx_to_function_and_idx[id];
                    string function = get<0>(function_and_idx);
                    int test_idx = get<1>(function_and_idx);
                    int instance_idx = get<2>(function_and_idx);
                    if (has_bugs(code)) {
                        tout << id << "| Function " << function << " test "<< test_idx <<" instance " << instance_idx <<" is crash-inconsistent!" << endl;
                        inconsistent_instances.push_back(make_tuple(id, test_idx));
                    } else {
                        tout << id << "| Function " << function << " test "<< test_idx <<" instance " << instance_idx <<" is crash-consistent!" << endl;
                    }
                    global_idx_to_state_and_res.erase(id);
                }
            }
        }
    }
    checker.join();

    tout<< "### Inconsistent Instance IDs ###" << endl;
    for (auto &p : inconsistent_instances) {
        tout << "Global instance id: " << get<0>(p) << " Test id: " << get<1>(p) << endl;
    }
    if (config_enabled("general.count_crash_state") || config_enabled("general.persevere")) {
        // if (config_enabled("general.persevere")) {
        //     tout << "### Crash State Info [Persevere] ###" << endl;
        // } else {
        //     tout << "### Crash State Info [Pathfinder] ###" << endl;
        // }
        // std::sort(crash_state_info.begin(), crash_state_info.end(), 
        //     [](const std::tuple<int, std::tuple<int, int>>& a, const std::tuple<int, std::tuple<int, int>>& b) {
        //           return std::get<0>(a) < std::get<0>(b);
        // });
        // int total_crash_states_tested = 0;
        // int total_crash_states = 0;
        // for (auto &p : crash_state_info) {
        //     tout << "Test id: " << get<0>(p) << " Crash states tested: " << get<0>(get<1>(p)) << " Total crash states: " << get<1>(get<1>(p)) << endl;
        //     total_crash_states_tested += get<0>(get<1>(p));
        //     total_crash_states += get<1>(get<1>(p));
        // }
        // tout << "Total crash states tested: " << total_crash_states_tested << " Total crash states: " << total_crash_states << endl;
        tout << "Crash state info [left:Pathfinder, right:Persevere]: " << endl;
        int total_crash_states_tested = 0;
        int total_crash_states = 0;
        for (auto &p : crash_state_info) {
            tout << "Test id: " << get<0>(p) << " Crash states tested by Pathfinder: " << get<0>(get<1>(p)) << ", Crash states tested by Persevere: " << get<1>(get<1>(p)) << endl;
            total_crash_states_tested += get<0>(get<1>(p));
            total_crash_states += get<1>(get<1>(p));
        }
        tout << "Total crash states tested by Pathfinder: " << total_crash_states_tested << " Total crash states tested by Persevere: " << total_crash_states << endl;
    }
    
    tout.flush();
    tout.close();
}

static bool has_zero_sized_element(const Module &m, const Type *t) {
    const StructType *st = dyn_cast<StructType>(t);
    if (!st) return false;

    for (Type *e : st->elements()) {
        if (0 == m.getDataLayout().getTypeSizeInBits(e).getFixedSize()) return true;
    }

    return false;
}

void engine::fill_args(
    const jinja2::ValuesMap &vals, list<string> &args, const char *key) const {
    if (config_is_empty(key)) {
        cerr << "Error: config key '" << key << "' is empty!";
        exit(EXIT_FAILURE);
    }
    string argstr = resolve_config_value(vals, key);
    boost::split(args, argstr, boost::is_space(), boost::token_compress_on);
}

void engine::try_fill_args(
    const jinja2::ValuesMap &vals, list<string> &args, const char *key) const {
    if (config_not_empty(key)) {
        fill_args(vals, args, key);
    }
}

shared_ptr<model_checker_state> engine::create_test(
    model_checker &checker,
    const pm_graph &graph,
    const update_mechanism &mechanism) const {

    auto vals = get_template_values(pmcheck_vals_.at("pmfile").asString());

    if (config_enabled("general.same_pmdir")) {
        vals.at("pmdir") = pmcheck_vals_.at("pmdir");
    }

    int start_idx = graph.get_event_idx(mechanism.front());
    int end_idx = graph.get_event_idx(mechanism.back());
    vector<int> event_idxs;
    for (const vertex &v : mechanism) {
        event_idxs.push_back(graph.get_event_idx(v));
    }

    list<string> setup_args, checker_args, daemon_args, cleanup_args;

    try_fill_args(vals, setup_args, "test.setup_tmpl");
    fill_args(vals, checker_args, "test.checker_tmpl");
    try_fill_args(vals, daemon_args, "test.daemon_tmpl");
    try_fill_args(vals, cleanup_args, "test.cleanup_tmpl");

    fs::path pmfile(vals.at("pmfile").asString());
    fs::path pmdir(vals.at("pmdir").asString());
    if (!pmdir.empty()) {
        fs::remove_all(pmdir);
        assert(!fs::exists(pmdir));
    }

    return checker.create_state(pmfile, pmdir, event_idxs,
        setup_args, checker_args, daemon_args, cleanup_args,
        vals, pmcheck_vals_, start_time);
}

shared_ptr<model_checker_state> engine::create_test(
    model_checker &checker,
    posix_graph &graph,
    vector<vertex> &vertex_vec) {
    auto vals = get_template_values(pmcheck_vals_.at("pmfile").asString());

    if (config_enabled("general.same_pmdir")) {
        vals.at("pmdir") = pmcheck_vals_.at("pmdir");
    }

    list<string> setup_args, checker_args, daemon_args, cleanup_args;

    try_fill_args(vals, setup_args, "test.setup_tmpl");
    fill_args(vals, checker_args, "test.checker_tmpl");
    try_fill_args(vals, daemon_args, "test.daemon_tmpl");
    try_fill_args(vals, cleanup_args, "test.cleanup_tmpl");

    fs::path pmfile(vals.at("pmfile").asString());
    fs::path pmdir(vals.at("pmdir").asString());
    if (!pmdir.empty()) {
        fs::remove_all(pmdir);
        assert(!fs::exists(pmdir));
    }

    atomic<bool> cancel_flag(false);
    set<set<vertex>> all_vertex_orders = graph.generate_all_orders(vertex_vec, cancel_flag);
    vector<vector<int>> all_event_orders;
    const_property_map pmap = boost::get(pnode_property_t(), graph.whole_program_graph());
    for (auto &order : all_vertex_orders) {
        vector<int> event_order;
        for (auto &v : order) {
            const posix_node *node = dynamic_cast<const posix_node*>(boost::get(pmap, v));
            event_order.push_back(node->event()->event_idx());
        }
        // sort by event index
        std::sort(event_order.begin(), event_order.end());
        all_event_orders.push_back(event_order);
    }


    return checker.create_state(pmfile, pmdir, all_event_orders,
        setup_args, checker_args, daemon_args, cleanup_args,
        vals, pmcheck_vals_, start_time);
}

shared_ptr<model_checker_state> engine::create_test(
    model_checker &checker,
    vector<int> event_idxs,
    int setup_until) const {

    auto vals = get_template_values(pmcheck_vals_.at("pmfile").asString());

    if (config_enabled("general.same_pmdir")) {
        vals.at("pmdir") = pmcheck_vals_.at("pmdir");
    }

    list<string> setup_args, checker_args, daemon_args, cleanup_args;

    try_fill_args(vals, setup_args, "test.setup_tmpl");
    fill_args(vals, checker_args, "test.checker_tmpl");
    try_fill_args(vals, daemon_args, "test.daemon_tmpl");
    try_fill_args(vals, cleanup_args, "test.cleanup_tmpl");

    fs::path pmfile(vals.at("pmfile").asString());
    fs::path pmdir(vals.at("pmdir").asString());
    if (!pmdir.empty()) {
        fs::remove_all(pmdir);
        assert(!fs::exists(pmdir));
    }

    return checker.create_state(pmfile, pmdir, event_idxs,
        setup_args, checker_args, daemon_args, cleanup_args,
        vals, pmcheck_vals_, start_time, setup_until);
}

shared_ptr<model_checker_state> engine::create_sanity_test(
    model_checker &checker) const {

    auto vals = get_template_values(pmcheck_vals_.at("pmfile").asString());

    if (config_enabled("general.same_pmdir")) {
        vals.at("pmdir") = pmcheck_vals_.at("pmdir");
        cout << "pmdir: " << vals.at("pmdir").asString() << endl;
    }

    // just create a dummy var here
    vector<int> event_idxs;
    event_idxs.push_back(0);

    list<string> setup_args, checker_args, daemon_args, cleanup_args;

    try_fill_args(vals, setup_args, "test.setup_tmpl");
    fill_args(vals, checker_args, "test.checker_tmpl");
    try_fill_args(vals, daemon_args, "test.daemon_tmpl");
    try_fill_args(vals, cleanup_args, "test.cleanup_tmpl");

    fs::path pmfile(vals.at("pmfile").asString());
    fs::path pmdir(vals.at("pmdir").asString());
    if (!pmdir.empty()) {
        fs::remove_all(pmdir);
        assert(!fs::exists(pmdir));
    }

    return checker.create_state(pmfile, pmdir, event_idxs,
        setup_args, checker_args, daemon_args, cleanup_args,
        vals, pmcheck_vals_, start_time);
}


void engine::finalize_results(
    const fs::path &final_out,
    const fs::path &tmp_out,
    const fs::path &success_file) const {

    if (config_enabled("general.output_to_tmpfs")) {
        recursive_copy(tmp_out, final_out);
        fs::remove_all(tmp_out);
        cout << "Results moved to final output directory: "
            << final_out.string() << endl;
    }

    touch_file(success_file);
}

int engine::run(void) {
    fs::path output_dir(resolve_config_value("general.output_dir_tmpl"));
    if (fs::exists(output_dir) && !config_enabled("general.overwrite")) {
        cerr << "error, output exists, will not overwrite\n";
        exit(EXIT_FAILURE);
    } else if (fs::exists(output_dir)) {
        fs::remove_all(output_dir);
    }

    fs::path final_output_dir = output_dir;
    if (config_enabled("general.output_to_tmpfs")) {
        output_dir = fs::path("/tmp") / fs::unique_path() / output_dir.filename();
        if (fs::exists(output_dir) && !config_enabled("general.overwrite")) {
            cerr << "error, TMPFS output exists, will not overwrite\n";
            exit(EXIT_FAILURE);
        } else if (fs::exists(output_dir)) {
            fs::remove_all(output_dir);
        }

        create_directories_or_error(final_output_dir);

        cout << "Sending temporary output to " << output_dir.string() << endl;
    }

    create_directories_or_error(output_dir);

    fs::path success_file = final_output_dir / "testing_completed";
    delete_if_exists(success_file);

    output_dir_ = output_dir;

    // For sanity/timing info
    fs::path info_file = output_dir / "info.txt";
    ofstream info(info_file.string());
    bio::tee_device<ostream, ofstream> tout_dev(cout, info);
    bio::stream<bio::tee_device<ostream, ofstream>> tout(tout_dev);

    bool verbose = config_enabled("general.verbose");
    time_point<system_clock> start = system_clock::now();
    start_time = start;

    /**
     * @brief Step 1: Trace the application under pmemcheck and retrieve the trace.
     *
     */
    trace t = gather_process_trace();

    time_point<system_clock> end_time = system_clock::now();

    // instead of construct a trace, we will just copy the log in gather_process_trace
    // output_trace(output_dir, t);
    tout << "Stage 1: Trace collection takes "
         << duration_cast<seconds>(end_time - start).count() << " seconds\n";
    tout << "Number of events: " << t.events().size() << "\n";
    tout << "Number of stores: " << t.stores().size() << "\n";
    tout.flush();
    // if (t.stores().empty()) {
    //     tout << "No stores!\n";
    //     tout.flush();
    //     return 1;
    // }

    /*
    * If enabled, skip all following steps and do reorder now
    */
    if (config_enabled("general.exhaustive_test") ||
        config_enabled("general.linear_test") ||
        config_enabled("general.jaaru_style_testing") ||
        config_enabled("general.fsync_test") ||
        config_enabled("general.fsync_reorder_test")) {
        int res = run_baseline_testing(t, output_dir, tout);

        finalize_results(final_output_dir, output_dir, success_file);

        return res;
    }

    if (config_enabled("general.sanity_test")) {
        model_checker checker(t, output_dir, chrono::seconds(config_int("test.timeout")), nullptr, PATHFINDER, mode_, op_tracing_);
        checker.save_pm_images = config_enabled("test.save_pm_images");
        checker.init_data = setup_file_data_;

        shared_ptr<model_checker_state> test = create_sanity_test(checker);
        shared_future<model_checker_code> res = checker.run_sanity_test(test);
        res.wait();
        auto code = res.get();
        if (has_bugs(code)) {
            cout << "Sanity test is crash-inconsistent!" << endl;
        } else {
            cout << "Sanity test is crash-consistent!" << endl;
        }

        checker.join();
        tout.flush();
        tout.close();

        finalize_results(final_output_dir, output_dir, success_file);

        return 0;
    }

    /**
     * @brief Step 2: Parse the trace into the the graph.
     *
     * Add type information.
     * TODO: There are some issues with the classification, fix these.
     */

    unique_ptr<Module> m;
    if (config_["general.mode"].as<string>() == "posix") {
        bool decompose_syscall = config_enabled("general.decompose_syscall");
        start_time = system_clock::now();
        pg_ = new posix_graph(t, output_dir, decompose_syscall);
        end_time = system_clock::now();
        tout << "Stage 2: Persistence graph generation takes "
             << duration_cast<seconds>(end_time - start_time).count() << " seconds\n";
        tout.flush();
        if (config_["general.test_ranges"].as<string>() != "") {
            string input = config_["general.test_ranges"].as<string>();
            assert(input.size() > 0 && input[0] == '[' && input[input.size()-1] == ']' && "Test ranges should start with '[' and end with ']'");
            input = input.substr(1, input.size()-2);
            cout << "Using test ranges, will skip representative testing!\n";
            vector<string> ranges;
            // split "[xxx],[xxx],[xxx]" into vector, each of it is a range of "[xxx]""
            boost::algorithm::split_regex(ranges, input, boost::regex("\\]\\s*,\\s*\\["));
            model_checker checker(t, output_dir, chrono::seconds(config_int("test.timeout")), pg_, PATHFINDER, mode_, op_tracing_);
            for (auto range : ranges) {
                // remove leading and trailing spaces
                boost::algorithm::trim(range); 
                // check if there is "-" in range
                vector<int> event_idxs;
                size_t pos = range.find("-");
                if (pos != string::npos) {
                    int start = stoi(range.substr(0, pos));
                    int end = stoi(range.substr(pos+1, range.size()-pos-1));
                    for (int i = start; i <= end; ++i) {
                        event_idxs.push_back(i);
                    }
                    cout << "Test range: [" << start << "-" << end << "]" << endl;
                }
                else {
                    // detect comma separated list
                    vector<string> event_strs;
                    boost::algorithm::split(event_strs, range, boost::is_any_of(","));
                    for (auto event_str : event_strs) {
                        boost::algorithm::trim(event_str);
                        event_idxs.push_back(stoi(event_str));
                    }
                    cout << "Test range: [";
                    for (auto event_idx : event_idxs) {
                        cout << event_idx << " ";
                    }
                    cout << "]" << endl;
                }
                vector<vertex> v;
                for (int i : event_idxs) {
                    if (t.events()[i]->is_marker_event()) {
                        continue;
                    }
                    v.push_back(pg_->get_vertex(t.events()[i]));
                }
                // call generator
                const_property_map pmap = boost::get(pnode_property_t(), pg_->whole_program_graph());
                atomic<bool> cancel_flag(false);
                set<set<vertex>> all_vertex_orders = pg_->generate_all_orders(v, cancel_flag);
                vector<vector<int>> all_event_orders;
                for (auto &order : all_vertex_orders) {
                    vector<int> event_order;
                    for (auto &v : order) {
                        const posix_node *node = dynamic_cast<const posix_node*>(boost::get(pmap, v));
                        event_order.push_back(node->event()->event_idx());
                    }
                    // sort by event index
                    std::sort(event_order.begin(), event_order.end());
                    all_event_orders.push_back(event_order);
                }

                // get the min index from set<set<vertex>> all_event_orders, for setup point
                int min_idx = INT_MAX;
                for (const auto &order : all_event_orders) {
                    for (const auto &idx : order) {
                        min_idx = std::min(min_idx, idx);
                    }
                }
                int test_idx = 0;
                for (auto &event_order : all_event_orders) {
                    shared_ptr<model_checker_state> test = create_test(checker, event_order, min_idx);
                    shared_future<model_checker_code> res = checker.run_test(test);
                    res.wait();
                    auto code = res.get();
                    if (has_bugs(code)) {
                        tout << "Instance " << test_idx <<" is crash-inconsistent!" << endl;
                    } else {
                        tout << "Instance " << test_idx <<" is crash-consistent!" << endl;
                    }
                    test_idx++;
                }

                // list<vertex> order;
                // while (!(order = og->nextOrder()).empty()) {
                //     for (vertex v : order) {
                //         std::cout << v << ' '; // Assuming vertices are labeled as 0 = A, 1 = B, etc.
                //     }
                //     std::cout << '\n';
                // }

            }
            checker.join();
            exit(EXIT_SUCCESS);
        }


        posix_graph *pg_ptr = dynamic_cast<posix_graph*>(pg_);
        start_time = system_clock::now();
        function_to_group_of_um_group fmap = get_update_mechanisms_by_function(t, *pg_ptr);
        end_time = system_clock::now();
        tout << "Stage 3: Identify representative update mechanisms takes "
             << duration_cast<seconds>(end_time - start_time).count() << " seconds\n";
        tout.flush();
        start_time = system_clock::now();
        run_representative_testing_by_function(t, fmap);
        end_time = system_clock::now();
        tout << "Stage 4: Representative testing by function takes "
             << duration_cast<seconds>(end_time - start_time).count() << " seconds\n";
        tout << "Total time: " << duration_cast<seconds>(end_time - start).count() << " seconds\n";
        tout.flush();

    }
    else {
        LLVMContext context;
        SMDiagnostic error;
        string bitcode_path = resolve_config_value("trace.bitcode_tmpl");
        m = std::move(parseIRFile(bitcode_path, error, context));
        if (!m) {
            tout << "Error: could not load module!" << endl;
            tout.flush();
            return 1;
        }

        if (config_enabled("general.trace_analysis")) {
            analyze_trace(output_dir, *m, t);
            // return 0;
        }
        pg_ = new pm_graph(*m, t, output_dir);
    }


    if (config_["general.mode"].as<string>() == "posix") {
        cerr << "POSIX mode, not implemented beyond this point" << endl;
        return 1;
    }

    pm_graph *graph_ptr = dynamic_cast<pm_graph*>(pg_);
    assert(graph_ptr);
    pm_graph &graph = *graph_ptr;
    
    // Musa: here we can proceed instead with random testing instead of
    // pathfindering (rep testing) if specified in the config file
    if (config_enabled("general.random_test")) {
        assert(config_["general.mode"].as<string>() == "pm" && "Random testing only works with pm mode now!");
        model_checker checker(t, output_dir, chrono::seconds(config_int("test.timeout")));
        checker.save_pm_images = config_enabled("test.save_pm_images");
        checker.init_data = setup_file_data_;

        int total_tests = 0;
        int num_bugs = 0;

        int res = run_random_testing(graph, checker, total_tests, num_bugs, tout);

        const time_point<system_clock> testing_end = system_clock::now();

        using namespace std::literals;

        tout << "\nTotal random tests: " << total_tests << "\n";
        tout << "Bugs found in random testing: " << num_bugs << "/" << total_tests << "\n";
        tout << "\nTotal time: " << (testing_end - start) / 1s << " seconds" << endl;

        tout.flush();
        tout.close();

        finalize_results(final_output_dir, output_dir, success_file);

        return res;
    }

    /**
     * @brief Step 3 and 4: Get the update mechanisms, then put the representative
     * first.
     *
     * Basically, just sort the update mechanism vectors in place so that the
     * first one is the representative vector.
     *
     * Only get the UMs that are within the selective testing region as well.
     */
    type_to_group_of_um_group update_mechanisms = get_update_mechanisms_by_type(t, graph);

    const time_point<system_clock> analysis_end = system_clock::now();

    tout << "\tAnalysis time: " << (analysis_end - start) / 1s << " seconds" << endl;

    tout << "***************************************************************\n";
    set<update_mechanism> unique_mechanisms;
    vector<update_mechanism_group> all_groups;
    vector<size_t> group_sizes, mechanism_sizes, rep_sizes;
    map<size_t, size_t> group_hist, mechanism_hist, rep_hist;
    for (const auto &p : update_mechanisms) {
        // const Type *t = p.first;
        const vector<update_mechanism_group> &groups = p.second;
        for (const update_mechanism_group &group : groups) {
            all_groups.push_back(group);
            group_hist[group.size()]++;
            group_sizes.push_back(group.size());

            rep_hist[group.front().size()]++;
            if (group.front().size() > 1) {
                rep_sizes.push_back(group.front().size());
            }

            for (const update_mechanism &m : group) {
                if (unique_mechanisms.count(m)) continue;

                unique_mechanisms.insert(m);
                mechanism_hist[m.size()]++;
                if (m.size() > 1) mechanism_sizes.push_back(m.size());

                // I want to see how many unique contexts there are.
                // Probably easiest to just dump memberships of stores.

            }
        }
    }

    output_groups(output_dir, graph, all_groups);

    std::sort(group_sizes.begin(), group_sizes.end());
    std::sort(mechanism_sizes.begin(), mechanism_sizes.end());
    std::sort(rep_sizes.begin(), rep_sizes.end());

    auto p_hist = [&] (std::string msg, const std::map<size_t, size_t> &hist) {
        vector<size_t> keys;
        size_t total = 0;
        for (const auto &p : hist) {
            keys.push_back(p.first);
            total += p.second;
        }
        std::sort(keys.begin(), keys.end());
        tout << msg << endl;
        for (size_t i : keys) {
            tout << "\t[" << i << "]: " << hist.at(i) << " ("
                << ((double)hist.at(i) / (double)total) * 100.0 << "%)" << endl;
        }
    };

    auto p_hist_cpy = [&] (std::string msg, const std::map<size_t, size_t> &hist) {
        vector<size_t> keys;
        size_t total = 0;
        for (const auto &p : hist) {
            keys.push_back(p.first);
            total += p.second;
        }
        std::sort(keys.begin(), keys.end());
        tout << msg << endl;
        for (size_t i : keys) {
            tout << i << "\t" << hist.at(i) << endl;
        }
    };

    size_t nmechanisms = unique_mechanisms.size();
    size_t ngroups = group_sizes.size();
    size_t nreps = ngroups;

    tout << "Number of Groups: " << ngroups << endl;
    tout << "Median Group Size: " << group_sizes[ngroups / 2] << endl;
    tout << "Average Group Size: " << std::accumulate(group_sizes.begin(),
        group_sizes.end(), 0) / ngroups << endl;
    p_hist("Group Sizes Histogram: ", group_hist);
    // p_hist_cpy("Group Sizes Histogram (copy version): ", group_hist);
    tout << endl;
    tout << "Number of Update Mechs: " << nmechanisms << endl;
    tout << "Median Mech Size (>1): " <<
        mechanism_sizes[mechanism_sizes.size() / 2] << endl;
    tout << "Average Mech Size (>1): " << std::accumulate(
        mechanism_sizes.begin(), mechanism_sizes.end(), 0)
        / mechanism_sizes.size() << endl;
    p_hist("Mech Sizes Histogram: ", mechanism_hist);
    // p_hist_cpy("Mech Sizes Histogram (copy version): ", mechanism_hist);
    tout << endl;
    tout << "Number of Representatives: " << nreps << endl;
    tout << "Median Representative Size (>1): " <<
        rep_sizes[rep_sizes.size() / 2] << endl;
    tout << "Average Representative Size (>1): " << std::accumulate(
        rep_sizes.begin(), rep_sizes.end(), 0) / rep_sizes.size() << endl;
    p_hist("Representative Sizes Histogram: ", rep_hist);
    // p_hist_cpy("Representative Sizes Histogram (copy version): ", rep_hist);
    tout << "***************************************************************\n";
    tout.flush();

    /**
     * @brief Step 5: Run the tests.
     *
     */
    model_checker checker(t, output_dir, chrono::seconds(config_int("test.timeout")));
    checker.save_pm_images = config_enabled("test.save_pm_images");
    checker.init_data = setup_file_data_;

    bool do_followup_testing = config_enabled("general.do_followup_testing");
    if (!do_followup_testing) {
        tout << "WARNING: skipping followup testing (from configuration)\n";
    }
    bool do_fn_testing = config_enabled("general.do_false_negative_testing");
    if (do_fn_testing) {
        tout << "WARNING: doing the false negative experiment! (from configuration)\n";
    }
    tout.flush();

    size_t nrep_tests = 0;

    uint64_t rep_id = 0;
    map<uint64_t, shared_ptr<model_checker_state>> rep_states;
    unordered_map<uint64_t, shared_future<model_checker_code>> rep_tests;

    unordered_map<uint64_t, update_mechanism_group> possible_followup_groups;
    list<update_mechanism> pending_followups;
    list<update_mechanism> all_followups;
    list<shared_ptr<model_checker_state>> followup_states;
    list<shared_future<model_checker_code>> followup_tests;

    size_t total_tests = 0;
    size_t max_range = 0;
    size_t max_size = 0;
    size_t represented = 0;
    size_t rep_bugs = 0;
    size_t followup_skipped = 0;

    /**
     * We also don't want to repeatedly cover the same store locations. So, before
     * we start testing a mechanism, let's check if all of it's vertices have
     * covered stack locations. If so, skip it, and add to the "skipped" list.
     */

    set<vector<stack_frame>> covered_locations;
    const_property_map pmap = boost::get(pnode_property_t(), graph.whole_program_graph());
    size_t skip_covered = 0;
    auto get_stack = [&] (vertex v) {
        const pm_node *p = dynamic_cast<const pm_node*>(boost::get(pmap, v));
        assert(p && "Node is null!");
        return p->event()->stack;
    };

    auto is_covered = [&] (const update_mechanism &m) {
        for (vertex v : m) {
            if (!covered_locations.count(get_stack(v))) return false;
        }
        return true;
    };

    auto add_to_covered = [&] (const update_mechanism &m) {
        for (vertex v : m) {
            covered_locations.insert(get_stack(v));
        }
    };

    size_t total_rep = 0;
    for (const auto &p : update_mechanisms) {
        total_rep += p.second.size();
    }
    cerr << "Will eventually set up " << total_rep << " rep tests!\n";
    // exit(EXIT_FAILURE);

    #define DO_KILL 0

    sort_um_by_ts sort_um(graph);
    vector<update_mechanism> rep_vec;
    // some book-keeping for followup tests and stats
    unordered_map<const update_mechanism, const update_mechanism_group*,
        update_mechanism_hash> rep_to_group;
    unordered_map<const update_mechanism, const Type*,
        update_mechanism_hash> rep_to_type;

    for (auto &p : update_mechanisms) {
        const Type *t = p.first;
        vector<update_mechanism_group> &groups = p.second;
        total_tests += groups.size();
        for (update_mechanism_group &g : groups) {
            update_mechanism &rep = g.front();
            if (is_covered(rep)) {
                skip_covered++;
                continue;
            } else {
                add_to_covered(rep);
            }
            rep_to_group[rep] = &g;
            rep_to_type[rep] = t;
            rep_vec.push_back(rep);
        }
    }

    std::sort(begin(rep_vec), end(rep_vec), sort_um);

    for (const auto &rep: rep_vec) {
        const Type *t = rep_to_type[rep];
        const update_mechanism_group &g = *rep_to_group[rep];

        tout << "Setting up test ID=" << nrep_tests << " for " <<
            get_type_name(t) << " at range ["
             << graph.get_event_idx(rep.front()) << ", "
             << graph.get_event_idx(rep.back()) << ") "
             << "size: " << graph.get_event_idx(rep.back()) -
                graph.get_event_idx(rep.front()) + 1
             << endl;
        tout.flush();

        max_range = std::max(max_range, rep.back() - rep.front());
        max_size = std::max(max_size, rep.size());
        represented += g.size() - 1;
        shared_ptr<model_checker_state> test = create_test(checker, graph, rep);
        shared_future<model_checker_code> res = checker.run_test(test);
        nrep_tests++;
        // cerr << "rep_tests.back = " << rep_tests.back().get() << "\n";
        update_mechanism_group followup(g.begin() + 1, g.end());
        possible_followup_groups[rep_id] = followup;
        // Poll
        if (!config_enabled("general.parallelize")) {
            res.wait();
            auto code = res.get();
            if (has_bugs(code)) {
                rep_bugs++;
                do_followup_testing = do_followup_testing && !all_inconsistent(code);
                const update_mechanism_group &umg = possible_followup_groups[rep_id];
                pending_followups.insert(pending_followups.end(), umg.begin(), umg.end());
                all_followups.insert(all_followups.end(), umg.begin(), umg.end());
                pending_followups.sort(update_mechanism_vertex_order);
            } else if (!do_fn_testing) {
                followup_skipped += followup.size();
                possible_followup_groups.erase(rep_id);
            }
        } else {
            rep_states[rep_id] = test;
            rep_tests[rep_id] = res;
            while (rep_states.size() > max_nproc_) {
                list<uint64_t> keys;
                for (const auto &p : rep_tests) {
                    keys.push_back(p.first);
                }
                for (uint64_t key : keys) {
                    shared_future<model_checker_code> &old_res = rep_tests.at(key);
                    if (!old_res.valid()) {
                        cerr << "Test " << key << " has no state!\n";
                        exit(EXIT_FAILURE);
                    }
                    auto status = old_res.wait_for(chrono::milliseconds(POLL_MILLIS));
                    if (status != future_status::ready) continue;
                    auto code = old_res.get();
                    if (has_bugs(code)) {
                        rep_bugs++;
                        do_followup_testing = do_followup_testing && !all_inconsistent(code);
                        const update_mechanism_group &umg = possible_followup_groups[key];
                        pending_followups.insert(pending_followups.end(), umg.begin(), umg.end());
                        all_followups.insert(all_followups.end(), umg.begin(), umg.end());
                        pending_followups.sort(update_mechanism_vertex_order);
                    } else if (!do_fn_testing) {
                        followup_skipped += possible_followup_groups[key].size();
                        possible_followup_groups.erase(key);
                    }
                    rep_states.erase(key);
                    rep_tests.erase(key);
                }
            }
        }
        rep_id++;
    }

    tout << "\nTotal: " << total_tests << "\n";
    tout << "\tMax size: " << max_size << "\n\tMax range: " << max_range << "\n";
    tout << "\tRepresented: " << represented << "\n";
    tout << "\tSpawned " << nrep_tests << " rep tests!\n";
    tout.flush();

    /**
     * @brief Step 6: Run followup tests if we find bugs in Step 4.
     *
     */
    size_t nfollowup_tests = 0;
    uint64_t followup_bugs = 0;
    uint64_t nalready_tested = 0;

    while (!rep_states.empty()) {

        list<uint64_t> keys;
        for (const auto &p : rep_tests) {
            keys.push_back(p.first);
        }

        for (uint64_t key : keys) {
            shared_future<model_checker_code> &res = rep_tests.at(key);
            if (!res.valid()) {
                cerr << "Test " << key << " has no state!\n";
                exit(EXIT_FAILURE);
            }
            auto status = res.wait_for(chrono::milliseconds(POLL_MILLIS));
            if (status != future_status::ready) {
                #if DO_KILL
                if (rep_tests.size() == 1 && pending_followups.empty()) {
                    // iangneal: fix this later.
                    tout << "\tGiving the last representative test one more chance...\n";
                    tout.flush();
                    status = res.wait_for(chrono::seconds(30));
                    if (status != future_status::ready) {
                        tout << "\t\tKilling the hanging test.\n";
                        tout.flush();
                        checker.kill();
                        // Avoid broken promises
                        rep_states.erase(key);
                        rep_tests.erase(key);
                        break;
                    }
                } else {
                    continue;
                }
                #else
                continue;
                #endif
            }

            auto code = res.get();
            if (has_bugs(code)) {
                rep_bugs++;
                do_followup_testing = do_followup_testing && !all_inconsistent(code);

                const update_mechanism_group &umg = possible_followup_groups[key];
                pending_followups.insert(pending_followups.end(), umg.begin(), umg.end());
                all_followups.insert(all_followups.end(), umg.begin(), umg.end());
                pending_followups.sort(update_mechanism_vertex_order);
            } else if (!do_fn_testing) {
                followup_skipped += possible_followup_groups[key].size();
                possible_followup_groups.erase(key);
            }

            rep_states.erase(key);
            rep_tests.erase(key);

            // Also spin up a followup test!
            // -- Spin up as many as we can.

            // -- Skip all the ones that are already covered.
            while (!pending_followups.empty() &&
                followup_states.size() + rep_tests.size() < max_nproc_)
            {
                while (!pending_followups.empty() && is_covered(pending_followups.front())) {
                    skip_covered++;
                    pending_followups.pop_front();
                }

                if (!pending_followups.empty()) {
                    const update_mechanism &f = pending_followups.front();
                    add_to_covered(f);

                    shared_ptr<model_checker_state> f_test = create_test(
                        checker, graph, f);
                    shared_future<model_checker_code> f_res =
                        checker.run_test(f_test);
                    nfollowup_tests++;

                    followup_states.push_back(f_test);
                    followup_tests.push_back(f_res);
                }
            }

            tout << "\t" << rep_states.size() << " remaining (also running " <<
                followup_states.size() << " followups)\n";
            tout.flush();
        }

        // Now, also poll our followup states.
        list<shared_ptr<model_checker_state>> remaining_states;
        list<shared_future<model_checker_code>> remaining_tests;

        auto state_it = followup_states.begin();
        auto test_it = followup_tests.begin();

        for (; state_it != followup_states.end(); ++state_it, ++test_it) {
            auto &old_res = *test_it;
            if (!old_res.valid()) {
                cerr << "Test has no state!\n";
                exit(EXIT_FAILURE);
            }
            auto status = old_res.wait_for(chrono::milliseconds(POLL_MILLIS));
            if (status != future_status::ready) {
                remaining_states.push_back(*state_it);
                remaining_tests.push_back(*test_it);
                continue;
            }

            if (has_bugs(old_res.get())) {
                followup_bugs++;
            }
        }

        followup_tests = remaining_tests;
        followup_states = remaining_states;
    }

    tout << "Bugs found in representative testing: " << rep_bugs << "/" <<
        nrep_tests << "\n";
    tout.flush();

    cerr << "Followup testing...\n";

    const time_point<system_clock> followup_start = system_clock::now();

    /**
     * First, collapse the remaining tests.
     *
     */
    uint64_t max_range_len = 0;
    uint64_t max_vertices = 0;
    for (const update_mechanism &m : all_followups) {
        max_range_len = std::max(max_range_len, m.back() - m.front());
        max_vertices = std::max(max_vertices, m.size());
    }

    tout << "\tTOTAL = " << all_followups.size() << endl;
    tout << "\t\tmax range:" << max_range_len << endl;
    tout << "\t\tmax num vertices:" << max_vertices << endl;
    tout.flush();

    #define ALL_INCONSISTENT_CHECK 0
    #if ALL_INCONSISTENT_CHECK
    if (!do_followup_testing) {
        tout << "\nSkipping followup testing.\n"
            << "\tThis could either be because:\n"
            << "\t\t(1) You disabled this in configuration, or\n"
            << "\t\t(2) Pathfinder detected at least one 'all inconsistent' test; "
                << "such tests usually result in very long followup test sequences, "
                << "so it's recommended that you patch the bug before performing followup testing.\n\n";
    } else {
    #endif
        tout << "\n\nRunning followup testing: " << all_followups.size()
            << " possible mechanisms to test!\n";
        tout.flush();
        while (!pending_followups.empty()) {
            update_mechanism mech = pending_followups.front();
            pending_followups.pop_front();
            if (is_covered(mech)) {
                skip_covered++;
                continue;
            } else {
                add_to_covered(mech);
            }

            shared_ptr<model_checker_state> test = create_test(checker, graph, mech);
            shared_future<model_checker_code> res = checker.run_test(test);
            nfollowup_tests++;

            if (!config_enabled("general.parallelize")) {
                res.wait();
                if (has_bugs(res.get())) {
                    followup_bugs++;
                }
            } else {
                followup_states.push_back(test);
                followup_tests.push_back(res);
                tout << "\tRunning " << followup_tests.size() << " follow up tests currently\n";
                tout << "\t\tSpawned " << nfollowup_tests << " follow up tests total (skipped "
                    << skip_covered << "; already covered)...\n";
                tout.flush();

                while (followup_tests.size() > max_nproc_) {
                    list<shared_ptr<model_checker_state>> remaining_states;
                    list<shared_future<model_checker_code>> remaining_tests;

                    auto state_it = followup_states.begin();
                    auto test_it = followup_tests.begin();

                    for (; state_it != followup_states.end(); ++state_it, ++test_it) {
                        auto &old_res = *test_it;
                        if (!old_res.valid()) {
                            cerr << "Test has no state!\n";
                            exit(EXIT_FAILURE);
                        }
                        auto status = old_res.wait_for(chrono::milliseconds(POLL_MILLIS));
                        if (status != future_status::ready) {
                            remaining_states.push_back(*state_it);
                            remaining_tests.push_back(*test_it);
                            continue;
                        }

                        if (has_bugs(old_res.get())) {
                            followup_bugs++;
                        }
                    }

                    followup_tests = remaining_tests;
                    followup_states = remaining_states;
                }
            }
        }

        tout << "\tSpawned " << nfollowup_tests << " followup tests!\n";
        tout << "\t\tSkipped " << followup_skipped << " for redundancy!\n";
        tout << "\t\tSkipped " << skip_covered << ", already covered!\n";
        tout.flush();

        // Now, wait for all the other tests to finish
        while (!followup_tests.empty()) {
            tout << "\tWaiting on " << followup_tests.size() << " remaining...\n";
            tout.flush();
            auto &old_res = followup_tests.front();
            if (!old_res.valid()) {
                cerr << "Test has no state!\n";
                exit(EXIT_FAILURE);
            }

            #if DO_KILL
            if (followup_tests.size() == 1) {
                // iangneal: fix this later.
                tout << "\tGiving the last followup test one more chance...\n";
                tout.flush();
                auto status = old_res.wait_for(chrono::seconds(30));
                if (status != future_status::ready) {
                    tout << "\t\tKilling the hanging test.\n";
                    checker.kill();
                    // So we don't get broken promises
                    followup_tests.pop_front();
                    followup_states.pop_front();
                    break;
                }
            }
            #endif

            auto status = old_res.wait_for(chrono::milliseconds(POLL_MILLIS));
            if (has_bugs(old_res.get())) {
                followup_bugs++;
            }

            followup_tests.pop_front();
            followup_states.pop_front();
        }

        tout << "Bugs found in followup testing: " << followup_bugs << "/"
            << nfollowup_tests << endl;
        tout.flush();
    #if ALL_INCONSISTENT_CHECK
    }
    #endif

    checker.join();

    const time_point<system_clock> testing_end = system_clock::now();

    using namespace std::literals;

    tout << "\nTotal time: " << (testing_end - start) / 1s <<
        " seconds" << endl;
    tout << "\tAnalysis time: " << (analysis_end - start) / 1s <<
        " seconds" << endl;
    tout << "\tTesting time: " << (testing_end - analysis_end) / 1s <<
        " seconds" << endl;
    tout << "\t\tRepresentative testing: " << (followup_start - analysis_end) /
        1s << " seconds" << endl;
    tout << "\t\tFollowup tesing: " << (testing_end - followup_start) / 1s <<
        " seconds" << endl;
    tout << "\n\tSkipped for coverage: " << skip_covered << endl;

    tout << "***************************************************************\n";
    tout << "Representatives tests: " << nrep_tests << endl;
    tout << "Total tests: " << (nfollowup_tests + nrep_tests) << endl;
    tout << "Total mechanisms: " << mechanism_sizes.size() << endl;
    tout << "Group Minimization: " << (double)(group_sizes.size()) /
        (double)(mechanism_sizes.size()) << endl;
    tout << "Test Minimization: " << (double)(nfollowup_tests + nrep_tests) /
        (double)(mechanism_sizes.size()) << endl;
    tout << "***************************************************************\n";

    tout.flush();
    tout.close();

    finalize_results(final_output_dir, output_dir, success_file);

    return 0;
}

int engine::run_baseline_testing(
        const trace &t,
        fs::path output_dir,
        bio::stream<bio::tee_device<ostream, ofstream>> &tout)
{   

    
    test_type ttype;
    if (config_enabled("general.exhaustive_test")) {
        ttype = EXHAUSTIVE;
    } else if (config_enabled("general.linear_test")) {
        ttype = LINEAR;
    } else if (config_enabled("general.jaaru_style_testing")) {
        ttype = PATHFINDER;
    } else if (config_enabled("general.fsync_test")) {
        ttype = FSYNC_TEST;
    } else if (config_enabled("general.fsync_reorder_test")) {
        ttype = FSYNC_REORDER_TEST;
    } else {
        cerr << __PRETTY_FUNCTION__ << ": bad ttype!" << endl;
        return -1;
    }

    model_checker checker(t, output_dir, chrono::seconds(config_int("test.timeout")), pg_, ttype, mode_, op_tracing_);
    checker.save_pm_images = config_enabled("test.save_pm_images");
    checker.baseline_timeout = chrono::minutes(config_int("general.baseline_timeout"));
    checker.init_data = setup_file_data_;

    // init indices
    int start_idx = 0;
    int end_idx = -1;
    int ntest = 0;
    int nbug = 0;

    // init graph objects
    bool decompose_syscall = config_enabled("general.decompose_syscall");
    pg_ = new posix_graph(t, output_dir, decompose_syscall);
    const_property_map pmap = boost::get(pnode_property_t(), pg_->whole_program_graph());
    posix_graph *pg_ptr = dynamic_cast<posix_graph*>(pg_);

    // global instance idx to start_idx, end_idx, and instance_idx
    unordered_map<int, tuple<int, int, int>> global_idx_to_range_and_idx;
    // global instance idx to (model_checker_state and result code)
    unordered_map<int, pair<shared_ptr<model_checker_state>, shared_future<model_checker_code>>> global_idx_to_state_and_res;


    const time_point<system_clock> start_time = system_clock::now();
    const auto timeout_point = start_time + std::chrono::minutes(
        config_int("general.baseline_timeout"));

    bool has_store = false;

    for (int i = 0; i < t.events().size(); i++) {
        if (system_clock::now() >= timeout_point) {
            tout << "Reach maximum timeout, cannot spawn more tests!" << endl;
            break;
        }

        const shared_ptr<trace_event> te = t.events()[i];

        // goto loop_end;

        if (ttype == EXHAUSTIVE || ttype == LINEAR || ttype == PATHFINDER) {
            if (!t.within_testing_range(te)) {
                start_idx++;
                has_store = false;
                continue;
            }

            has_store = has_store || te->is_store();

            if (!te->is_fence() && !te->is_pathfinder_end()) {
                continue;
            }

            end_idx = te->is_pathfinder_end() ? i : i + 1;
            start_idx = t.events()[start_idx]->is_pathfinder_begin() ? start_idx + 1 : start_idx;

            if (end_idx - start_idx <= 0) {
                continue;
            }

            // skip ranges that have no stores
            if (!has_store) {
                start_idx = i + 1;
                continue;
            }

            // reset the has_store thing
            has_store = false;
        } 
        else if (ttype == FSYNC_TEST || ttype == FSYNC_REORDER_TEST) {
            // we only split the test when we see fsync or fdatasync
            if (!te->is_fsync() && !te->is_fdatasync() && !te->is_msync()) {
                continue;
            }

            end_idx = te->is_pathfinder_end() ? i : i + 1;
            start_idx = t.events()[start_idx]->is_pathfinder_begin() ? start_idx + 1 : start_idx;

            if (end_idx - start_idx <= 0) {
                continue;
            }
        }
        else {
            cerr << "Unknown test type!" << endl;
            return -1;
        }

loop_end:
        // TODO: fix EXHAUSTIVE, LINEAR, JAARU baselines
        switch (ttype) {
        case EXHAUSTIVE:
            tout << "Running exhaustive test ";
            break;
        case LINEAR:
            tout << "Running linear test ";
            break;
        case PATHFINDER:
            tout << "Running Jaaru test ";
            break;
        case FSYNC_TEST:
            tout << "Running fsync test ";
            break;
        case FSYNC_REORDER_TEST:
            tout << "Running fsync reorder test";
            break;
        default:
            tout << "Running UNKNOWN test ";
            break;
        }

        if (ttype == FSYNC_REORDER_TEST) {
            // init vertices for reordering
            vector<vertex> v;
            for (int i = start_idx; i <= end_idx; i++) {
                v.push_back(pg_->get_vertex(t.events()[i]));
            }

            // get reorderings
            vector<vector<int>> all_event_orders;
            if (v.size() > max_um_size_) { // if there are too many update mechanisms
                if (v.size() > LINEAR_MAX_SIZE) {
                    tout << "Too many events in range:[" << start_idx << "," << end_idx  << "] test "<< ntest << " skip for now" << endl;
                    ntest++;
                    continue;
                }
                // just do linear order for now if the size is too big
                vector<int> event_order;
                for (auto &v : v) {
                    const posix_node *node = dynamic_cast<const posix_node*>(boost::get(pmap, v));
                    event_order.push_back(node->event()->event_idx());
                    all_event_orders.push_back(event_order);
                }
            } else { // do the reordering
                atomic<bool> cancel_flag(false);
                set<set<vertex>> all_vertex_orders = pg_ptr->generate_all_orders(v, cancel_flag);
                for (auto &order : all_vertex_orders) {
                    vector<int> event_order;
                    for (auto &v : order) {
                        const posix_node *node = dynamic_cast<const posix_node*>(boost::get(pmap, v));
                        event_order.push_back(node->event()->event_idx());
                    }
                    // sort by event index
                    std::sort(event_order.begin(), event_order.end());
                    all_event_orders.push_back(event_order);
                }
            }

            // get the min index from set<set<vertex>> all_event_orders, for setup point
            int min_idx = INT_MAX;
            for (const auto &order : all_event_orders) {
                for (const auto &idx : order) {
                    min_idx = std::min(min_idx, idx);
                }
            }

            // create tests
            int instance_idx = 0;
            for (auto &event_order : all_event_orders) {
                shared_ptr<model_checker_state> test = create_test(checker, event_order, min_idx);
                if (!config_enabled("general.parallelize")) {
                    shared_future<model_checker_code> res = checker.run_test(test);
                    res.wait();
                    auto code = res.get();
                    if (has_bugs(code)) {
                        tout << ntest << "| FSYNC Reordering Range:[" << start_idx << "," << end_idx << "] test instance " << instance_idx <<" is crash-inconsistent!" << endl;
                    } else {
                        tout << ntest << "| FSYNC Reordering Range:[" << start_idx << "," << end_idx << "] test instance " << instance_idx <<" is crash-consistent!" << endl;
                    }
                } else {
                    global_idx_to_range_and_idx[ntest] = make_tuple(start_idx, end_idx, instance_idx);
                    shared_future<model_checker_code> res = checker.run_test(test);
                    global_idx_to_state_and_res[ntest] = make_pair(test, res);
                    while (global_idx_to_state_and_res.size() > max_nproc_) {
                        vector<int> ids;
                        for (auto &p : global_idx_to_state_and_res) {
                            ids.push_back(p.first);
                        }
                        for (int id : ids) {
                            auto &p = global_idx_to_state_and_res[id];
                            if (p.second.wait_for(chrono::milliseconds(POLL_MILLIS)) == future_status::ready) {
                                auto code = p.second.get();
                                auto &range_and_idx = global_idx_to_range_and_idx[id];
                                int start_idx = get<0>(range_and_idx);
                                int end_idx = get<1>(range_and_idx);
                                int instance_idx = get<2>(range_and_idx);
                                if (has_bugs(code)) {
                                    tout << id << "| FSYNC Reordering Range:[" << start_idx << "," << end_idx << "] test instance " << instance_idx <<" is crash-inconsistent!" << endl;
                                } else {
                                    tout << id << "| FSYNC Reordering Range:[" << start_idx << "," << end_idx << "] test instance " << instance_idx <<" is crash-consistent!" << endl;
                                }
                                global_idx_to_state_and_res.erase(id);
                            }
                        }
                    }
                }
                instance_idx++;
                ntest++;
            }
        } else if (ttype == FSYNC_TEST) {
            // init empty event_idxs, create tests
            vector<int> v;
            shared_ptr<model_checker_state> test = create_test(checker, v, end_idx);
            
            // run tests
            if (!config_enabled("general.parallelize")) {
                shared_future<model_checker_code> res = checker.run_test(test);
                res.wait();
                auto code = res.get();
                if (has_bugs(code)) {
                    tout << ntest << "| FSYNC Range:[" << end_idx << "," << end_idx << "] test instance " << 0 <<" is crash-inconsistent!" << endl;
                } else {
                    tout << ntest << "| FSYNC Range:[" << end_idx << "," << end_idx << "] test instance " << 0 <<" is crash-consistent!" << endl;
                }
            } else {
                global_idx_to_range_and_idx[ntest] = make_tuple(end_idx, end_idx, 0);
                shared_future<model_checker_code> res = checker.run_test(test);
                global_idx_to_state_and_res[ntest] = make_pair(test, res);
                while (global_idx_to_state_and_res.size() > max_nproc_) {
                    vector<int> ids;
                    for (auto &p : global_idx_to_state_and_res) {
                        ids.push_back(p.first);
                    }
                    for (int id : ids) {
                        auto &p = global_idx_to_state_and_res[id];
                        if (p.second.wait_for(chrono::milliseconds(POLL_MILLIS)) == future_status::ready) {
                            auto code = p.second.get();
                            auto &range_and_idx = global_idx_to_range_and_idx[id];
                            int start_idx = get<0>(range_and_idx);
                            int end_idx = get<1>(range_and_idx);
                            int instance_idx = get<2>(range_and_idx);
                            if (has_bugs(code)) {
                                tout << ntest << "| FSYNC Range:[" << end_idx << "," << end_idx << "] test instance " << 0 <<" is crash-inconsistent!" << endl;
                            } else {
                                tout << ntest << "| FSYNC Range:[" << end_idx << "," << end_idx << "] test instance " << 0 <<" is crash-consistent!" << endl;
                            }
                            global_idx_to_state_and_res.erase(id);
                        }
                    }
                }
            }
            ntest++;
        } else {
            // TODO: Handle exhaustive, linear, pathfinder cases
        }
        
        start_idx = i + 1;
    }

    // what is this for?
    if (config_enabled("general.parallelize")) {
        while (global_idx_to_state_and_res.size() > 0) {
            vector<int> ids;
            for (auto &p : global_idx_to_state_and_res) {
                ids.push_back(p.first);
            }
            for (int id : ids) {
                auto &p = global_idx_to_state_and_res[id];
                if (p.second.wait_for(chrono::milliseconds(POLL_MILLIS)) == future_status::ready) {
                    auto code = p.second.get();
                    auto &range_and_idx = global_idx_to_range_and_idx[id];
                    int start_idx = get<0>(range_and_idx);
                    int end_idx = get<1>(range_and_idx);
                    int instance_idx = get<2>(range_and_idx);
                    if (ttype == FSYNC_TEST) {
                        if (has_bugs(code)) {
                            tout << id << "| FSYNC Range:[" << end_idx << "," << end_idx << "] test instance " << instance_idx <<" is crash-inconsistent!" << endl;
                        } else {
                            tout << id << "| FSYNC Range:[" << end_idx << "," << end_idx << "] test instance " << instance_idx <<" is crash-consistent!" << endl;
                        }
                    } else if (ttype == FSYNC_REORDER_TEST) {
                        if (has_bugs(code)) {
                            tout << id << "| FSYNC Reordering Range:[" << start_idx << "," << end_idx << "] test instance " << instance_idx <<" is crash-inconsistent!" << endl;
                        } else {
                            tout << id << "| FSYNC Reordering Range:[" << start_idx << "," << end_idx << "] test instance " << instance_idx <<" is crash-consistent!" << endl;
                        }
                    }
                    global_idx_to_state_and_res.erase(id);
                }
            }
        }
    }

    // log some stats
    switch (ttype) {
    case EXHAUSTIVE:
        tout << "\nTotal exhaustive tests: ";
        break;
    case LINEAR:
        tout << "\nTotal linear tests: ";
        break;
    case PATHFINDER:
        tout << "\nTotal Jaaru tests: ";
        break;
    case FSYNC_TEST:
        tout << "\nTotal fsync tests: ";
        break;
    default:
        tout << "\nTotal UNKNOWN tests: ";
        break;
    }

    tout << ntest << "\nTotal bugs found: "
        << nbug << "/" << ntest << "\n";

    const time_point<system_clock> end_time = system_clock::now();
    tout << "\nTotal time: " << (end_time - start_time) / 1s << " seconds" << endl;

    checker.join();
    tout.flush();
    tout.close();

    return 0;
}

int engine::run_random_testing(
    pm_graph &graph,
    model_checker &checker,
    int &total_tests,
    int &num_bugs,
    bio::stream<bio::tee_device<ostream, ofstream>> &tout) {

    // Musa: for random testing, sample random nodes from persistence graph until timeout
    // first, put all nodes into a vector
    // then, after testing each node, remove it from the vector
    vector<vertex> verts;

    graph_type::vertex_iterator it, end;
    for (boost::tie(it, end) = boost::vertices(graph.whole_program_graph()); it != end; ++it) {
        const vertex &v = *it;
        verts.push_back(v);
    }

    // AGAIN, LESSON LEARNT: hold the test ptr!!
    map<int, shared_ptr<model_checker_state>> id_to_test;
    map<int, shared_future<model_checker_code>> id_to_res;

    auto end_time = system_clock::now() + std::chrono::minutes(config_int("general.baseline_timeout"));

    while (system_clock::now() < end_time && !verts.empty()) {
        // create an update mechanism with a single, random vertex
        update_mechanism m;
        int index = rand() % verts.size();
        m.push_back(verts.at(index));

        // test this new update mechanism
        shared_ptr<model_checker_state> test = create_test(checker, graph, m);
        shared_future<model_checker_code> res = checker.run_test(test);
        tout << "Running random test " << total_tests << " on vertex " << graph.get_event_idx(verts.at(index)) << endl;
        tout.flush();

        if (!config_enabled("general.parallelize")) {
            res.wait();
            auto code = res.get();
            if (has_bugs(code)) {
                num_bugs++;
            }
        } else {
            id_to_test[total_tests] = test;
            id_to_res[total_tests] = res;
            while (id_to_test.size() > max_nproc_) {
                list<int> finish_ids;
                for (auto iter : id_to_res) {
                    shared_future<model_checker_code> old_res = iter.second;
                    const auto status = old_res.wait_for(chrono::milliseconds(POLL_MILLIS));
                    if (status != future_status::ready) {
                        continue;;
                    }
                    auto code = old_res.get();
                    if (has_bugs(code)) {
                        tout << "Test " << iter.first << " is crash-inconsistent! "<< endl;
                        num_bugs++;
                    } else {
                        tout << "Test " << iter.first << " is crash-consistent! "<< endl;
                    }
                    tout.flush();
                    finish_ids.push_back(iter.first);
                }
                for (int id : finish_ids) {
                    id_to_res.erase(id);
                    id_to_test.erase(id);
                }
            }
        }

        // remove the vertex we tested from verts
        verts.erase(verts.begin() + index);
        total_tests++;
    }

    // wait for rest of the tests
    while (!id_to_res.empty()) {
        list<int> finish_ids;
        for (auto iter : id_to_res) {
            shared_future<model_checker_code> old_res = iter.second;
            const auto status = old_res.wait_for(chrono::milliseconds(POLL_MILLIS));
            if (status != future_status::ready) {
                continue;;
            }
            auto code = old_res.get();
            if (has_bugs(code)) {
                tout << "Test " << iter.first << " is crash-inconsistent! "<< endl;
                num_bugs++;
            } else {
                tout << "Test " << iter.first << " is crash-consistent! "<< endl;
            }
            tout.flush();
            finish_ids.push_back(iter.first);
        }
        for (int id : finish_ids) {
            id_to_res.erase(id);
            id_to_test.erase(id);
        }
    }

    checker.join();
    return 0;
}

void engine::analyze_trace(fs::path output_dir, const Module &m, const trace &t) {
    type_crawler tc(m, t);
    fs::path analysis_file = output_dir / "store_analysis.csv";
    error_if_exists(analysis_file);
    fs::ofstream f(analysis_file, ios_base::app);
    for (const auto & te : t.stores()) {
        type_crawler::type_info_set tis = tc.all_types(te);
        // output in the format of STORE_ID, NUMBER_OF_TYPES, TYPE_NAME1, TYPE_NAME2, ...
        f << te->store_id() << "," << tis.size();
        for (const auto & ti : tis) {
            f << "," << ti.str();
        }
        f << endl;
    }
    f.flush();
    f.close();
}

void engine::output_trace(fs::path output_dir, trace t) {
    fs::path trace_file = output_dir / "full_trace.csv";
    error_if_exists(trace_file);
    t.dump_csv(trace_file);
}

void engine::output_groups(fs::path output_dir, const pm_graph &graph, const std::vector<update_mechanism_group> &groups) {
    fs::path group_file = output_dir / "groups.csv";
    error_if_exists(group_file);
    fs::ofstream f(group_file);
    // header
    f << "group_id,group_representative,group_members\n";

    const_property_map pmap = boost::get(pnode_property_t(), graph.whole_program_graph());

    auto get_store_id = [&] (vertex v) {
        const pm_node *p = dynamic_cast<const pm_node*>(boost::get(pmap, v));
        assert(p && "Node is null!");
        return p->event()->store_id();
    };

    auto output_mechanism = [&] (const update_mechanism &m) {
        for (size_t i = 0; i < m.size(); ++i) {
            f << get_store_id(m[i]);
            if (i + 1 < m.size()) {
                f << ";";
            }
        }
    };

    for (size_t i = 0; i < groups.size(); ++i) {
        const auto &group = groups.at(i);
        // group_id
        f << i << ",";
        // group_representative
        output_mechanism(group.front());
        f << ",";
        // group_members
        for (size_t j = 1; j < group.size(); ++j) {
            output_mechanism(group.at(j));
            if (j + 1 < group.size()) {
                f << "|";
            }
        }
        f << "\n";
    }

    f.flush();
    f.close();
}

}
