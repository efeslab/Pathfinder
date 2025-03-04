#include "model_checker.hpp"

#include <algorithm>
#include <cassert>
#include <cerrno>
#include <chrono>
#include <fstream>
#include <unordered_map>
#include <cstdint>
#include <sstream>
#include <string>
#include <sys/mman.h>
#include <sys/uio.h>
#include <thread>
#include <vector>
#include <iomanip>
#include <errno.h>

#include <boost/asio.hpp>
#include <boost/process.hpp>
#include <boost/archive/iterators/base64_from_binary.hpp>
#include <boost/archive/iterators/insert_linebreaks.hpp>
#include <boost/archive/iterators/transform_width.hpp>
#include <boost/archive/iterators/ostream_iterator.hpp>

namespace aio = boost::asio;
namespace ait = boost::archive::iterators;
namespace bp = boost::process;
namespace fs = boost::filesystem;
namespace icl = boost::icl;
using namespace std::chrono;
using namespace std;

#define DEBUG_PRINTS 0

namespace pathfinder {

/* model_checker */


// Common initialization function for model_checker_state
model_checker_state* model_checker::initialize_state(
    fs::path pmfile,
    fs::path pmdir,
    list<string> setup_args,
    list<string> checker_args,
    list<string> daemon_args,
    list<string> cleanup_args,
    jinja2::ValuesMap checker_vals,
    jinja2::ValuesMap pmcheck_vals,
    time_point<system_clock> start_time) {

    auto *state = new model_checker_state(next_id_, trace_, output_dir_, stdout_mutex_);

    if (!pmdir.empty()) {
        error_if_exists(pmdir);
        create_directories_or_error(pmdir);
    }

    unordered_map<string, fs::path> pmfile_map = trace_.map_pmfiles(pmdir);
    unordered_map<string, fs::path> fsfile_map = trace_.map_fsfiles(pmdir);

    state->pmdir = pmdir;
    state->fsfile_map = fsfile_map;
    state->pmfile_map = pmfile_map;
    state->checker_args = checker_args;
    state->daemon_args = daemon_args;
    state->checker_args = checker_args;
    state->cleanup_args = cleanup_args;
    state->setup_args = setup_args;
    state->save_file_images = save_pm_images;
    state->timeout = timeout_;
    state->baseline_timeout = baseline_timeout;
    state->start_time = start_time;
    state->ttype = ttype_;
    state->mode_ = mode_;
    state->op_tracing_ = op_tracing_;
    state->persevere_ = persevere_;

    // Setup init data
    // TODO: implement for new pmdir stuff
    if (!init_data.empty()) {
        std::ofstream o;
        if (pmfile_map.size() != 1) {
            cerr << "N pmfiles: " << pmfile_map.size() << "\n";
            BOOST_ASSERT(pmfile_map.size() == 1);
            exit(EXIT_FAILURE);
        }
        o.open(pmfile.c_str(), ios::binary);
        o.write(init_data.data(), init_data.size());
        o.flush();
        o.close();
    }

    #if DEBUG_PRINTS
    cerr << "(spawning) TEST ID " << next_id_ << endl;
    cerr << "\tNumber of events: " << event_idxs.size() << endl;
    cerr << "\tEvent range: "  << event_idxs.back() - event_idxs.front() << endl;
    #endif

    return state;
}

shared_ptr<model_checker_state> model_checker::create_state(
    fs::path pmfile,
    fs::path pmdir,
    vector<int> event_idxs,
    list<string> setup_args,
    list<string> checker_args,
    list<string> daemon_args,
    list<string> cleanup_args,
    jinja2::ValuesMap checker_vals,
    jinja2::ValuesMap pmcheck_vals,
    time_point<system_clock> start_time,
    int setup_until) {

    auto *state = initialize_state(pmfile, pmdir, setup_args, checker_args, daemon_args, cleanup_args, checker_vals, pmcheck_vals, start_time);
    state->event_idxs = event_idxs; // Set the event_idxs specifically for this state

    if (setup_until != -1) {
        state->setup_until = setup_until;
    }

    // Debug prints or any additional initialization specific to event_idxs
    #if DEBUG_PRINTS
    cerr << "\tNumber of events: " << event_idxs.size() << endl;
    #endif

    next_id_++;
    return shared_ptr<model_checker_state>(state);
}

shared_ptr<model_checker_state> model_checker::create_state(
    fs::path pmfile,
    fs::path pmdir,
    vector<vector<int>> all_event_orders,
    list<string> setup_args,
    list<string> checker_args,
    list<string> daemon_args,
    list<string> cleanup_args,
    jinja2::ValuesMap checker_vals,
    jinja2::ValuesMap pmcheck_vals,
    time_point<system_clock> start_time) {

    auto *state = initialize_state(pmfile, pmdir, setup_args, checker_args, daemon_args, cleanup_args, checker_vals, pmcheck_vals, start_time);
    state->all_event_orders = all_event_orders; // Set the all_event_orders specifically for this state

    // Debug prints or any additional initialization specific to all_event_orders
    #if DEBUG_PRINTS
    cerr << "\tNumber of order sets: " << all_event_orders.size() << endl;
    #endif

    next_id_++;
    return shared_ptr<model_checker_state>(state);
}

shared_future<model_checker_code> model_checker::run_test(
    shared_ptr<model_checker_state> state) {

    promise<model_checker_code> p;
    shared_future<model_checker_code> f = p.get_future().share();
    if (mode_ == POSIX) {
        if (state->all_event_orders.empty()) {
            thread t(&model_checker_state::run_posix, state.get(), std::move(p));
            threads_.push_back(std::move(t));
        }
        else {
            thread t(&model_checker_state::run_posix_with_orders, state.get(), std::move(p));
            threads_.push_back(std::move(t));
        }
    }
    else {
        thread t(&model_checker_state::run_pm, state.get(), std::move(p));
        threads_.push_back(std::move(t));
    }

    return f;
}

shared_future<model_checker_code> model_checker::run_sanity_test(
    shared_ptr<model_checker_state> state) {

    promise<model_checker_code> p;
    shared_future<model_checker_code> f = p.get_future().share();
    thread t(&model_checker_state::run_sanity_check, state.get(), std::move(p));
    threads_.push_back(std::move(t));
    return f;
}

void model_checker::dump_event_file(void) const {
    // Initialize the file (header)
    fs::path efile = output_dir_ / "events.csv";
    BOOST_ASSERT(!fs::exists(efile));
    // cerr << "efile: " << efile.string() << "\n";

    fs::ofstream f(efile);
    f << "store_id";
    // f << "store_id,event_num";

    // Figure out the max stack frame
    size_t max_stack = 0;
    for (const auto &te : trace_.stores()) {
        max_stack = std::max(max_stack, te->stack.size());
    }

    f << ",address,size,value";

    for (size_t i = 0; i < max_stack; ++i) {
        f << ",frame_" << i;
        f << ",function_" << i;
        f << ",file_" << i;
        f << ",line_" << i;
        f << ",binary_address_" << i;
    }
    f << "\n";

    // Now dump all the events
    for (const auto &te : trace_.stores()) {
        f << te->store_id();
        // f << "," << te->event_idx();
        f << "," << te->address;
        f << "," << te->size;
        f << "," << te->value;
        size_t i = 0;
        // fill in what we have
        for (; i < te->stack.size(); ++i) {
            f << ",\"" << te->stack[i].str() << "\"";
            f << ",\"" << te->stack[i].function << "\"";
            f << ",\"" << te->stack[i].file << "\"";
            f << "," << te->stack[i].line;
            f << ",\"" << te->stack[i].binary_address << "\"";
        }
        // then, fill with empty
        for (; i < max_stack; ++i) {
            f << ",,,,,";
        }
        f << "\n";
    }

    f.flush();
    f.close();
}

model_checker::model_checker(const trace &t, fs::path outdir, chrono::seconds timeout, const persistence_graph *pg, test_type ttype, pathfinder_mode mode, bool op_tracing, bool persevere)
        : trace_(t), output_dir_(outdir), timeout_(timeout), pg_(pg), ttype_(ttype), mode_(mode), op_tracing_(op_tracing), persevere_(persevere) {
    dump_event_file();

    stdout_mutex_ = make_shared<mutex>();

    // Check that the pintool stuff is available.
    if (!fs::exists(PIN_PATH)) {
        cerr << __PRETTY_FUNCTION__ << ": could not find pin path \"" <<
            PIN_PATH << "\"!" << endl;
        exit(EXIT_FAILURE);
    }

    if (!fs::exists(PINTOOL_DPOR_MMIO_PATH)) {
        cerr << __PRETTY_FUNCTION__ << ": could not find pintool path \"" <<
            PINTOOL_DPOR_MMIO_PATH << "\"!" << endl;
        exit(EXIT_FAILURE);
    }
}

void model_checker::join(void) {
    for (auto &t : threads_) {
        if (t.joinable()) {
            t.join();
        }
    }
}

void model_checker::kill(void) {
    for (thread &t : threads_) {
        auto pthread_handle = t.native_handle();

        if (pthread_handle != 0) {
            pthread_cancel(pthread_handle);
        }

        if (t.joinable()) {
            t.join();
        }
    }
}

}  // namespace pathfinder