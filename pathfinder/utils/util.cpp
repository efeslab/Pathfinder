#include "util.hpp"

#include <boost/asio.hpp>
#include <boost/filesystem/fstream.hpp>
#include <boost/iostreams/copy.hpp>
#include <boost/iostreams/device/array.hpp>
#include <boost/iostreams/filter/gzip.hpp>
#include <boost/iostreams/filtering_streambuf.hpp>
#include <boost/iostreams/stream.hpp>
#include <cstdlib>
#include <execinfo.h>
#include <iostream>
#include <sstream>
#include <thread>

namespace aio = boost::asio;
namespace bio = boost::iostreams;
namespace bp = boost::process;
namespace fs = boost::filesystem;
namespace ip = boost::asio::ip;
namespace po = boost::program_options;
using namespace std::chrono;
using namespace std;

namespace boost
{

void assertion_failed_msg(char const * expr, char const * msg, char const * function, char const * file, long line) {
    fprintf(stderr, "'%s' failed, fn=%s at %s:%ld. %s\n", expr, function, file, line, msg);
    exit(1);
}

void assertion_failed(char const * expr, char const * function, char const * file, long line) {
    assertion_failed_msg(expr, "", function, file, line);
}

}

namespace pathfinder {

void dump_backtrace(size_t depth) {
    void **array = new void*[depth];
    size_t size = backtrace(array, depth);
    backtrace_symbols_fd(array, size, STDERR_FILENO);
    delete[] array;
}

bp::native_environment get_pm_env(void) {
    auto env = boost::this_process::environment();
    env["PMEM_IS_PMEM_FORCE"] = "1";
    env["PMEM_MMAP_HINT"] = PMEM_MMAP_HINT_ADDRSTR;
    return env;
}

vector<string> split(string s, string delimiter) {
    vector<string> res;
    boost::split(res, s, boost::is_any_of(delimiter));
    return res;
}

void check_file_exists(const string &filename, const string &msg = "") {
    if (!fs::exists(filename)) {
        // check on $PATH
        string pathstr = getenv("PATH");
        for (const auto &prefix : split(pathstr, ":")) {
            fs::path sys_path = fs::path(prefix) / filename;
            if (fs::exists(sys_path)) {
                return;
            }
        }

        cerr << "Error: file " << filename << " does not exist, required to continue!\n";
        if (msg.size()) {
            cerr << "Message: " << msg << "\n";
        }
        exit(EXIT_FAILURE);
    }
}

bp::child start_command(const list<string> &args, bp::ipstream &outs, bp::ipstream &errs) {
    check_file_exists(args.front());
    bp::child c(bp::args(args), get_pm_env(), bp::std_out > outs, bp::std_err > errs);
    return c;
}

bp::child start_command(const list<string> &args) {
    check_file_exists(args.front());
    bp::child c(bp::args(args), get_pm_env(), bp::std_err > bp::null, bp::std_out > bp::null);
    return c;
}

int finish_command(bp::child &c, bp::ipstream &outs, bp::ipstream &errs, string &output) {
    stringstream ss;

    c.wait();

    string line;
    while (std::getline(outs, line) && !line.empty()) {
        ss << "[STDOUT] " << line << "\n";
        // cerr << "[STDOUT] " << line << "\n";
        line.clear();
    }
    while (std::getline(errs, line) && !line.empty()) {
        ss << "[STDERR] " << line << "\n";
        // cerr << "[STDERR] " << line << "\n";
        line.clear();
    }

    ss.flush();
    output = ss.str();

    return c.exit_code();
}

int finish_command(bp::child &c, bp::ipstream &outs,
    bp::ipstream &errs, string &output, std::chrono::seconds timeout) {
    stringstream ss;

    auto start = steady_clock::now();

    while (c.running()) {
        std::this_thread::sleep_for(chrono::milliseconds(10));
        auto now = steady_clock::now();
        auto time_diff = duration_cast<milliseconds>(now - start);
        auto timeout_ms = duration_cast<milliseconds>(timeout);
        if (time_diff >= timeout_ms) {
            break;
        }
    }

    if (c.running()) {
        c.terminate();
        c.wait();
    }

    string line;
    while (std::getline(outs, line) && !line.empty()) {
        ss << "[STDOUT] " << line << "\n";
        // cerr << "[STDOUT] " << line << "\n";
        line.clear();
    }
    while (std::getline(errs, line) && !line.empty()) {
        ss << "[STDERR] " << line << "\n";
        // cerr << "[STDERR] " << line << "\n";
        line.clear();
    }

    ss.flush();
    output = ss.str();

    return c.exit_code();
}

int finish_command(bp::child &c, std::chrono::seconds timeout) {
    // while (c.running()) {
    //     bool finished = c.wait_for(chrono::milliseconds(10));
    //     if (system_clock::now() - start >= timeout) break;
    // }

    // if (c.running()) {
    //     c.terminate();
    //     c.wait();
    // }
    // iangneal: It seems this may have uncovered a boost bug. So just wait.
    // if (c.running()) {
    //     c.wait();
    // }

    time_point<system_clock> start = system_clock::now();

    while (c.running()) {
        std::this_thread::sleep_for(chrono::milliseconds(10));
        if (system_clock::now() - start >= timeout) break;
    }

    if (c.running()) {
        c.terminate();
        c.wait();
    }

    return c.exit_code();
}

int run_command(string cmd) {
    list<string> args;
    boost::split(args, cmd, boost::is_any_of(" "));
    bp::child c = start_command(args);
    c.wait();
    return c.exit_code();
}

int run_command(const std::list<std::string> &args) {
    bp::child c = start_command(args);
    c.wait();
    return c.exit_code();
}

int run_command(const std::list<std::string> &args, std::chrono::seconds timeout) {
    bp::child c = start_command(args);
    return finish_command(c, timeout);
}

int run_command(const std::list<std::string> &args, std::string &output) {
    bp::ipstream out_is, err_is;

    bp::child c = start_command(args, out_is, err_is);
    return finish_command(c, out_is, err_is, output);
}

int run_command(const std::list<std::string> &args,
    std::string &output, std::chrono::seconds timeout) {
    bp::ipstream out_is, err_is;

    bp::child c = start_command(args, out_is, err_is);
    return finish_command(c, out_is, err_is, output, timeout);
}

vector<char> gzip::compress(const vector<char>& data) {
    vector<char> out_data;
    bio::stream<bio::array_source> origin(data.data(), data.size());

    bio::filtering_streambuf<bio::input> out;
    out.push(bio::gzip_compressor(bio::gzip_params(bio::gzip::best_compression)));
    out.push(origin);
    bio::copy(out, std::back_inserter<vector<char>>(out_data));

    return out_data;
}

vector<char> gzip::decompress(const vector<char>& data) {
    vector<char> out_data;
    bio::stream<bio::array_source> origin(data.data(), data.size());

    bio::filtering_streambuf<bio::input> out;
    out.push(bio::gzip_decompressor());
    out.push(origin);
    bio::copy(out, std::back_inserter<vector<char>>(out_data));

    return out_data;
}

bool copy_directory(fs::path const &source, fs::path const &dest, bool exists_sparse_file) {
    if (!fs::exists(source) || !fs::is_directory(source)) {
        cerr << "copy_directory: Source is not a directory!" << endl;
        return false;
    }

    if (!fs::exists(dest)) fs::create_directory(dest);

    if (!fs::is_directory(dest)) {
        cerr << "copy_directory: Dest is not a directory!" << endl;
        return false;
    }

    // if contain sparse file, we must use command line arguments, as I cannot find a copy function in boost library for sparse files
    if (exists_sparse_file) {
        std::list<string> args{
            "cp", "--sparse=always", "-r", source.string() + "/.", dest.string()};
        std::string output;
        int ret_code = run_command(args);
        if (!ret_code) return true;
        else {
            cerr << "copy_directory: Command line copy failed!" << endl;
            cerr << "error message: " << output << endl;
            return false;
        }
    }

    for (auto &entry: boost::make_iterator_range(fs::directory_iterator(source), {})) {
        fs::path entry_path = entry.path();
        try {
            if (fs::is_directory(entry_path)) {
                if (!pathfinder::copy_directory(entry_path, dest / entry_path.filename(), exists_sparse_file)) return false;
            }
            else {
                fs::copy(entry_path, dest / entry_path.filename());
            }
        }
        catch (fs::filesystem_error const & e) {
            cerr << e.what() << endl;
            return false;
        }
    }
    return true;
}

void recursive_copy(const fs::path &src, const fs::path &dst) {
    if (fs::exists(dst)) {
        throw std::runtime_error(dst.generic_string() + " exists");
    }

    if (fs::is_directory(src)) {
        fs::create_directories(dst);
        for (fs::directory_entry& item : fs::directory_iterator(src)) {
            recursive_copy(item.path(), dst/item.path().filename());
        }
    } else if (fs::is_regular_file(src)) {
        fs::copy(src, dst);
    } else {
        throw std::runtime_error(dst.generic_string() + " not dir or file");
    }
}

void delete_if_exists(const fs::path &path) {
    if (fs::exists(path)) {
        bool res = fs::remove(path);
        if (!res) {
            cerr << __FUNCTION__ << ": Unable to remove file " << path << "!\n";
            exit(EXIT_FAILURE);
        }
    }
}

void touch_file(const boost::filesystem::path &path) {
    fs::ofstream(path).close();
}

void error_if_exists(const boost::filesystem::path &path) {
    if (fs::exists(path)) {
        cerr << __FUNCTION__ << ": file \"" << path << "\" should not exist!\n";
        dump_backtrace();
        exit(EXIT_FAILURE);
    }
}

void create_directories_or_error(const fs::path &path) {
    error_if_exists(path);
    if (!fs::create_directories(path)) {
        cerr << __FUNCTION__ << ": Could not create directory \"" <<
            path << "\"!\n";
        dump_backtrace();
        exit(EXIT_FAILURE);
    }
}

void create_directories_if_not_exist(const fs::path &path) {
    if (!fs::exists(path) && !fs::create_directories(path)) {
        cerr << __FUNCTION__ << ": Could not create directory \"" <<
            path << "\"!\n";
        dump_backtrace();
        exit(EXIT_FAILURE);
    }
}

void print_variable_map(const po::variables_map &vm, const std::string &msg) {
    cout << msg << "\n";
    for (const auto &entry : vm) {
        cout << "\t" << entry.first << " => ";

        const auto &a = entry.second.value();
        if (auto v = boost::any_cast<int>(&a)) {
            cout << "int: " << *v;
        } else if (auto v = boost::any_cast<bool>(&a)) {
            cout << "bool: " << *v;
        } else if (auto v = boost::any_cast<string>(&a)) {
            cout << "str: \"" << *v << "\"";
        } else if (auto v = boost::any_cast<fs::path>(&a)) {
            cout << "fs::path: " << *v;
        } else {
            cout << "<error type>";
        }

        cout << "\n";
    }
}

// https://stackoverflow.com/questions/18183174/how-do-i-correctly-randomly-assign-a-port-to-a-test-http-server-using-boost-asio/19923459
unsigned short get_open_port(void) {
    aio::io_service service;
    ip::tcp::acceptor acceptor(service, ip::tcp::endpoint(ip::tcp::v4(), 0));
    unsigned short port = acceptor.local_endpoint().port();
    return port;
}

}