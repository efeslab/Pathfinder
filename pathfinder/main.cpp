#include <cstdlib>
#include <iostream>
#include <string>
#include <thread>

#include <boost/any.hpp>
#include <boost/filesystem.hpp>
#include <boost/program_options.hpp>

#include "utils/common.hpp"
#include "utils/util.hpp"
#include "runtime/pathfinder_engine.hpp"

using namespace std;
namespace po = boost::program_options;
namespace fs = boost::filesystem;

static po::options_description get_config_file_opt(const fs::path &config_path) {
    po::options_description config_opt("Configuration file settings");

    /*
    Get programmatic defaults
    */

    unsigned nthreads = std::thread::hardware_concurrency();

    /*
    The foo.bar syntax allows for sectioned INI files.

    For example,
        ... --foo.bar X

    is equivalent to:
        ...
        [foo]
        bar = X
        ...
    */
    // TODO: support non-tmpl args?
    config_opt.add_options()
        // general, tool-wide settings
        // --- options
        ("general.verbose", po::value<bool>()->default_value(false),
            "print status messages and debugging messages")
        ("general.overwrite", po::value<bool>()->default_value(true),
            "overwrite results file if it already exists")
        ("general.use_real_pmem", po::value<bool>()->default_value(true), "run on PM")
        ("general.parallelize", po::value<bool>()->default_value(true),
            "run as many tasks in parallel as possible")
        ("general.do_followup_testing", po::value<bool>()->default_value(true),
            "run followup testing (or not)")
        ("general.do_false_negative_testing", po::value<bool>()->default_value(false),
            "test all followup arguments.")
        ("general.init_file_state", po::value<fs::path>()->default_value(fs::path("")),
            "initial states of files needed when use_pmdir is true.")
        ("general.use_fscall", po::value<bool>()->default_value(true),
            "target program may use filesystem syscalls to do writes.")
        ("general.num_cmdfiles", po::value<int>()->default_value(2),
            "number of files to fill in at program, e.g. if 2, fill in {{pmfile0}}, {{pmfile1}}")
        ("general.baseline_timeout", po::value<int>()->default_value(15),
            "timeout for baselines (in minutes), default is 15 minutes")
        // --- non-templated
        ("general.pwd", po::value<fs::path>()->default_value(config_path.parent_path()),
            "pwd, defaults to parent of config file")
        ("general.pm_fs_path", po::value<fs::path>(), "path to the PM file system")
        // --- templated
        ("general.output_dir_tmpl", po::value<string>(),
            "results file output path (templated)")
        ("general.max_nproc", po::value<int>()->default_value(nthreads / 2),
            "max number of threads to use")
        ("general.output_to_tmpfs", po::value<bool>()->default_value(false),
            "output results to TMPFS, then move to permanent storage at the end")
        ("general.use_induced_subgraph", po::value<bool>()->default_value(false),
            "use the induced subgraph relation for finding representativeness")
        // enable this option when there are metadata being written during trace generation, currently only support empty initial directory state
        ("general.same_pmdir", po::value<bool>()->default_value(false), "use the same pmdir for testing as for tracing.")


        // --- types of testing
        ("general.persevere", po::value<bool>()->default_value(false),
            "A re-implementation of Persevere, currently only support crash state calculation but not actual testing.")
        ("general.selective_testing", po::value<bool>()->default_value(false),
            "only find and perform testing for update mechanisms in a range "
            "specified in the program (default=false)\n"
            "\t(Jaaru does this to skip expensive initialization).")
        ("general.exhaustive_test", po::value<bool>()->default_value(false),
            "do an exhaustive test based on the trace, test reorderings split by FENCE")
        ("general.linear_test", po::value<bool>()->default_value(false),
            "do a linear test that apply stores linearly, test orderings split be FENCE")
        ("general.random_test", po::value<bool>()->default_value(false),
            "do random testing.")
        ("general.sanity_test", po::value<bool>()->default_value(false),
            "do a sanity test to replay all events on the trace")
        ("general.jaaru_style_testing", po::value<bool>()->default_value(false),
            "do a test that tests all epochs with model checker, which "
            "implements the DPOR optimizations of Jaaru.")
        ("general.fsync_test", po::value<bool>()->default_value(false),
            "POSIX: do a baseline test that splits on fsync.")
        ("general.fsync_reorder_test", po::value<bool>()->default_value(false),
            "POSIX: do a baseline test that splits and reorders events between fsyncs.")
        ("general.mode", po::value<string>()->default_value("pm"),
            "different testing modes: pm, mmio, or posix")
        ("general.trace_analysis", po::value<bool>()->default_value(false),
            "analyze memory access pattern of the trace")
        ("general.test_ranges", po::value<string>()->default_value(""),
            "specify test ranges in the format of '[...],[...]', where '[...]' could either be '[start-end]' or '[event1, event2, event3, ...]'" )
        ("general.op_tracing", po::value<bool>()->default_value(false),
            "trace operations in the workload that are completed and inform the checker, this is for checker that does advanced verification")
        ("general.decompose_syscall", po::value<bool>()->default_value(true), "decompose syscalls into micro events")
        ("general.count_crash_state", po::value<bool>()->default_value(false), "count number of crash states tested and number of crash states being represented")
        ("general.max_um_size", po::value<int>()->default_value(40),
            "max number of events in an update mechanism that will be model checked")

        // tracing settings (i.e., pmemcheck / Pin tool)
        // --- options
        ("trace.verbose", po::value<bool>()->default_value(false), "print process output")
        ("trace.trace_path", po::value<string>()->default_value(""), "skip trace generation and use offline trace specified by the path")
        ("trace.root_dir", po::value<string>()->default_value(""), "root dir used in offline trace, useful for pathfinder to derive file and dir relations between workload and checker")
        // --- templated
        ("trace.cmd_tmpl", po::value<string>(), "program + args (required). If daemon is not set, this is traced")
        ("trace.daemon_tmpl", po::value<string>()->default_value(""), "path to daemon program + args (templated)")
        ("trace.setup_tmpl", po::value<string>()->default_value(""), "command for test setup, run once (templated)")
        ("trace.setup_daemon_tmpl", po::value<string>()->default_value(""), "command for test setup, run once (templated)")
        ("trace.cleanup_tmpl", po::value<string>()->default_value(""), "command for test setup, run once (templated)")
        ("trace.bitcode_tmpl", po::value<string>(), "path to the program bitcode (required)")

        // model checking settings (i.e., reordering procedure)
        // --- non-templated
        ("test.timeout", po::value<int>()->default_value(30), "timeout per check in seconds (default=30)")
        ("test.save_pm_images", po::value<bool>()->default_value(false), "save the compressed PM images for offline debugging")
        // --- templated
        ("test.checker_tmpl", po::value<string>(), "path to validation program + args (templated)")
        ("test.daemon_tmpl", po::value<string>()->default_value(""), "path to daemon program + args (templated)")
        ("test.setup_tmpl", po::value<string>()->default_value(""), "command for test setup, run once (templated)")
        ("test.cleanup_tmpl", po::value<string>()->default_value(""), "command for test setup, run once (templated)")
    ;

    return config_opt;
}

static void print_usage(const char *prog,
                        const po::options_description &desc,
                        const po::positional_options_description &pos) {
    cout << "Usage: " << prog << " [options]";
    for (unsigned i = 0; i < pos.max_total_count(); ++i ) {
        cout << " <" << pos.name_for_position(i) << ">";
    }
    cout << endl << desc;
}

typedef po::basic_command_line_parser<char> command_line_parser;

int main(int argc, const char *argv[]) {
    po::options_description cmdline("Command line options");

    // This weird syntax is repeated operator() usages.
    cmdline.add_options()
        ("version,v", "show the current version")
        ("help,h", "display general help message")
        ("config-help,H", "display config file help")
        ("config-file", po::value<fs::path>(), "configuration file (for all other settings)")
    ;

    po::positional_options_description pos;
    // also needs to be defined as part of the options_description
    pos.add("config-file", -1);

    po::variables_map args;
    vector<string> unrecognized;
    try {
        po::parsed_options parsed = command_line_parser(argc, argv)
            .options(cmdline)
            .positional(pos)
            .allow_unregistered()
            .run();
        po::store(parsed, args);
        // invokes callbacks and the like.
        po::notify(args);

        unrecognized = po::collect_unrecognized<char>(parsed.options, po::exclude_positional);

    } catch (const boost::wrapexcept<boost::program_options::multiple_occurrences> &e) {
        cerr << e.what() << "\n\n";
        cerr << "If you encountered this error while trying to provide overrides\n"
        << "for configuration arguments (e.g., --general.selective_testing 1),\n"
        << "please use '=' syntax to specify values (e.g., --general.selective_testing=1)\n\n";
        e.rethrow();
    }

    // for (const auto &s : unrecognized) {
    //     cout << "unrecognized " << s << '\n';
    // }

    if (args.count("help")) {
        print_usage(argv[0], cmdline, pos);
        return 1;
    }

    if (args.count("config-help")) {
        po::options_description config_opt = get_config_file_opt(fs::path("."));
        print_usage(argv[0], config_opt, po::positional_options_description());
        return 1;
    }

    if (args.count("version")) {
        cout << "Pathfinder (PM) version: "
            << PATHFINDER_VERSION_MAJOR << "." << PATHFINDER_VERSION_MINOR << endl;
        return 1;
    }

    if (!args.count("config-file")) {
        cerr << "Error: no config file specified!" << endl;
        print_usage(argv[0], cmdline, pos);
        return 1;
    }

    fs::path config_path = args["config-file"].as<fs::path>();
    if (!fs::exists(config_path)) {
        cerr << "Error: no such file '" << config_path << "'!" << endl;
        return 1;
    }
    config_path = fs::absolute(config_path);

    po::options_description config_opt = get_config_file_opt(config_path);

    po::variables_map config;
    // Parse any extra args to override existing args from the file.
    // -- Set this first, so it has priority over the config file
    po::store(
        command_line_parser(unrecognized)
            .options(config_opt)
            .run(),
        config);
    // Now set the rest
    po::store(po::parse_config_file(config_path.c_str(), config_opt), config);
    // Required for callbacks or something. Read Boost docs.
    po::notify(config);

    pathfinder::print_variable_map(config, "Current Pathfinder Engine Configuration:");

    // Call the engine
    return pathfinder::engine(config).run();
}
