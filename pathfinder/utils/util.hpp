#pragma once

#include <chrono>
#include <list>
#include <string>
#include <vector>

// This include fixes an include issue in boost/process
#include <algorithm>
#include <boost/program_options.hpp>
#include <boost/process.hpp>

#include "common.hpp"

namespace pathfinder {

void dump_backtrace(size_t depth=10);

boost::process::native_environment get_pm_env(void); 

int run_command(std::string cmd);
int run_command(const std::list<std::string> &args);
int run_command(const std::list<std::string> &args, std::string &output);
int run_command(const std::list<std::string> &args, std::chrono::seconds timeout);
int run_command(const std::list<std::string> &args,
				std::string &output,
				std::chrono::seconds timeout);

boost::process::child start_command(
	const std::list<std::string> &args,
	boost::process::ipstream &out_stream,
	boost::process::ipstream &err_stream
);

boost::process::child start_command(
	const std::list<std::string> &args
);

int finish_command(
	boost::process::child &process,
	std::chrono::seconds timeout
);

int finish_command(
	boost::process::child &process,
	boost::process::ipstream &out_stream,
	boost::process::ipstream &err_stream,
	std::string &output
);

int finish_command(
	boost::process::child &process,
	boost::process::ipstream &out_stream,
	boost::process::ipstream &err_stream,
	std::string &output,
	std::chrono::seconds timeout
);

// https://gist.github.com/yfnick/6ba33efa7ba12e93b148
struct gzip {
	static std::vector<char> compress(const std::vector<char>& data);

	static std::vector<char> decompress(const std::vector<char>& data);
};

// recursively copy files from source directory to target directory
bool copy_directory(boost::filesystem::path const &source, boost::filesystem::path const &dest, bool exists_sparse_file=false);

void recursive_copy(const boost::filesystem::path &src, const boost::filesystem::path &dst);

void delete_if_exists(const boost::filesystem::path &path);

void touch_file(const boost::filesystem::path &path);

void error_if_exists(const boost::filesystem::path &path);

void create_directories_or_error(const boost::filesystem::path &path);

void create_directories_if_not_exist(const boost::filesystem::path &path);

void print_variable_map(const boost::program_options::variables_map &vm, const std::string &msg);

unsigned short get_open_port(void);

}  // namespace pathfinder