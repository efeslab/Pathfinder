# Pathfinder

[![arXiv](https://img.shields.io/badge/arXiv-2503.01390-b31b1b.svg)](https://arxiv.org/abs/2503.01390)
[![DOI](https://img.shields.io/badge/DOI-10.1145/3720431-blue.svg)](https://doi.org/10.1145/3720431)

## Introduction

This version contains necessary code and step to reproduce the main scientific claims in the paper.

Pathfinder is a scalable and accurate application-level crash-consistency tool. It leverages representative testing: a new crash-state space reduction strategy based on the key observation is that the consistency of crash states is often correlated, even if those crash states are not identical. Pathfinder supports testing both POSIX-based applications and MMIO-based applications.

There are mainly two kinds of applications evaluated: POSIX-based applications and MMIO-based applications.

For POSIX-based applications, this version contains workload, trace obtained from the workload and evaluation configuration for Pathfinder to reproduce all the bugs detected.
The configuration also supports counting crash states and evaluates our implementation of Persevere by changing parameters.
It additionally contains code coverage data collected from the hardware setup in the paper.

The ALICE tool baseline could be found at [https://github.com/efeslab/alice](https://github.com/efeslab/alice).

For MMIO-based applications, this version contains the full implementation for performing crash-consistency testing. However, we do not include MMIO-based workloads in this version as running them requiring access to machines that are equipped with persistent memory.
*Therefore, the main target of this artifact is to obtain the Artifact Available and Artifact Evaluated badges*.

*We also provide an dedicated server with environment setup for artifact evaluation. Please see `ARTIFACTS.md` for more details.*

## Hardware Dependencies


For POSIX-based applications, we run our experiments on a server with an Intel Xeon Platinum 8480+ CPU (2.00 GHz). However, any Linux-based machine with Ubuntu 22.04 should generally work.

For MMIO-based applications, we run our experiments on a server with an Intel Xeon Gold 6230 CPU (2.10 GHz) and 4×128GB Intel Optane Series 100 Pmem DIMMs.


## Directory structure

- `cmake`: Contains some custom CMake functions uses to build targets.
- `deps`: Project dependencies, notably PMDK.
- `pathfinder`: Core source code directory
- `targets`: These are the workloads Pathfinder tests. This contains source code and `pathfinder-config.ini` files, which tells Pathfinder how to test the targets.

## Getting Started Guide
### Setup instructions

1. Install libraries

```
sudo apt update
sudo apt install cmake clang-13 llvm-13-dev libboost-all-dev libmlpack-dev libb64-dev libglib2.0-dev  libgtk2.0-dev zlib1g-dev  libc++-dev
sudo ln -s /usr/lib/gcc/x86_64-linux-gnu/11/libstdc++.so /usr/lib/x86_64-linux-gnu/libstdc++.so
sudo pip install wllvm
```

2. Set up the Pathfinder GitHub directory, including submodules.

```sh
git clone git@github.com:efeslab/Pathfinder.git
cd Pathfinder
git submodule init
git submodule update
```

### Build instructions

```sh
cd Pathfinder #if you're not there already
mkdir build
cd build
cmake ..
make pathfinder-core -j
```

Note: the make process may generate some warnings (e.g., `WARNING:Did not recognize the compiler flag "-dM"`). This is expected (a consequence of using WLLVM for compilation).

### Build Pin tool tracer

Pathfinder also relies on Pin tools to trace the applications.
Navigates to `pin_tool/README.md` for instructions on how to compile the Pin tool.

### Finding bugs

The targets in `targets` define a `pathfinder-config.ini` file to define run commands for testing. 

#### To find bugs
```sh
cd Pathfinder/build
./pathfinder/pathfinder-core targets/<some target directory>/<some config file>

# example:
./pathfinder/pathfinder-core targets/leveldb-bug-0/pathfinder-config.ini
```

## Step by Step Instructions
This section contains detailed instructions on how to run Pathfinder for crash-consistency testing, and how to interpret the results. 
We will use `leveldb-bug-0` as an example, but the procedure will be very similar for other bugs.

### Reproduce Bug Detection Result
#### Prepare Application Library and Workload
To reproduce the results presented in Section 6.1 of the paper, first navigates to the corresponding bug directory under `targets`.
As an example, `targets/leveldb-bug-0` contains `workload.cc` as the workload program and `checker.cc` as the checker program. 
They have a dependency on the `leveldb` repo. 

First follow the `targets/leveldb-bug-0/README.md` to download and compile the debug version of `leveldb` at 
the specific commit.
After that, change the `targets/leveldb-bug-0/Makefile` to point to `leveldb` source folder by updating `{{ LEVELDB_SRC_PATH }}`. 
Compile the workload and checker program using `make`.

#### Pathfinder Config File

After the compilation is done, we are ready to run the application. 
Pathfinder takes in a config file to specify configuration parameters for the crash-consistency testing. 
Below shows an example config file from `targets/leveldb-bug-0/pathfinder-config.ini`.
We will go over a few important parameters, but the full list can be found in `pathfinder/main.cpp`.

```
[general]
output_dir_tmpl = {{ build_root }}/leveldb_bug_0
verbose = yes
pm_fs_path = {{YOUR_PATH}}
max_nproc = 80
parallelize = yes
count_crash_state = no
persevere = no
mode = posix

[trace]
trace_path = {{ pwd }}/traces/tracer.log
root_dir = /home/yilegu/squint_test_dir/628b-bda1-d221-d700
verbose = yes
cmd_tmpl = {{ pwd }}/workload {{ pmdir }}

[test]
checker_tmpl = {{ pwd }}/checker {{ pmdir }}
timeout = 30
```

The configuration file uses jinjia template to auto fill the missing arguments.


`output_dir_impl` specifiy where the result will be generated.
It will currently be generated at `Pathfinder/build/leveldb_bug_0` as `{{ build_root }}` is automatically filled.

`pm_fs_path` specifies where Pathfinder could store intermediate results during the testing. Please create an empty folder and replace the `{{YOUR_PATH}}` with an absolute path.

`parallelize` enables parallelized testing of multiple crash states and `max_nproc` specifies the maximum number of processes used.

`count_crash_state` specifies whether Pathfinder is counting crash states tested vs. total number of crash states during testing.

`persevere` specifies whether Pathfinder uses its own implementation of Persevere algorithm to perform the testing.

Under `[trace]` category, `trace_path` specifies if we are using offline collected logs to perform the testing and `root_dir` specifies what root directory is used for storing data when generating the trace.
`root_dir` can be derived directly from the log.

If `trace_path` is not provided, it will at runtime collect the trace by running the workload using commands in `cmd_tmpl`.

`[test]` category specifies checker commands.

#### Run Testing

We can now run Pathfinder by the following command

```
build/pathfinder/pathfinder-core targets/leveldb-bug-0/pathfinder-config.ini
```

#### Understand Results

This will generate results under `build/leveldb_bug_0`. 

`info.txt` provides a summary of the results, recoding what are the crash states being tested and what are the checker results. Search for `crash-inconsistent` for cases where the checker reports a crash-consistency bug.

`x_y.csv` csv files documents detailed info about each crash state being tested.

For example, if `info.txt` shows `29| Function leveldb::DBImpl::NewDB test 4 instance 14 is crash-inconsistent!`. This means that the 29th crash state being tested is crash-inconsistent.

Then we navigates to `29_0.csv`, which shows the following 

```
ret_code,message,note,timestamp(posix mode)
2,5,6,9,1,2,3,4,1,"[STDOUT] Open failed
[STDOUT] Corruption: no meta-nextfile entry in descriptor
","posix",10
```

Sequence `2,5,6,9,1,2,3,4` (other than the trailing `1`) shows the order of the operations being applied. You can find what are the operations in `tracer.log`. `1` is the return code showing the error, followed by error message in stdout and stderr. At last is the timestamp, in this case, it takes `10` seconds for Pathfinder to detect this crash-consistency bug.

`stack_tree_x.log` shows the stack call tree being generated (Section 4.1). 

`subgraph_x.dot` shows all subgraphs being tested (Section 4.1).
You can visualize the subgraphs by using the script provided `tools/generate_subgraph.sh build/leveldb_bug_0`.

#### Crash States
To count the number of crash states, specify `count_crash_state = yes` in the config file `targets/leveldb-bug-0/pathfinder-config.ini`.

Then in the `info.txt`, it will show number of crash states tested for each test case vs. total numbder of crash states in `### Crash State Info ###`. Below shows an example

```
### Crash State Info ###
Test id: 0 Crash states tested: 7 Total crash states: 62
Test id: 1 Crash states tested: 23 Total crash states: 53
Test id: 2 Crash states tested: 43 Total crash states: 18748
Test id: 7 Crash states tested: 12 Total crash states: 54
Test id: 8 Crash states tested: 25 Total crash states: 25
Test id: 10 Crash states tested: 147 Total crash states: 147
Test id: 13 Crash states tested: 62 Total crash states: 5580
Test id: 15 Crash states tested: 89 Total crash states: 89
Test id: 16 Crash states tested: 42 Total crash states: 120
Test id: 17 Crash states tested: 46 Total crash states: 46
Test id: 18 Crash states tested: 34 Total crash states: 131
Test id: 19 Crash states tested: 6 Total crash states: 90
Test id: 20 Crash states tested: 9 Total crash states: 33
Test id: 22 Crash states tested: 158 Total crash states: 3476
Test id: 23 Crash states tested: 83 Total crash states: 235
Test id: 24 Crash states tested: 19 Total crash states: 35
Test id: 27 Crash states tested: 56 Total crash states: 186
Test id: 28 Crash states tested: 35 Total crash states: 131
Test id: 29 Crash states tested: 18 Total crash states: 152
Test id: 30 Crash states tested: 48 Total crash states: 176
Total crash states tested: 962 Total crash states: 29569
```


#### Persevere
Running Persevere baseline is done by specifying `persevere = yes` in the config file `targets/leveldb-bug-0/pathfinder-config.ini`.


## Reusability Guide

### Testing new applications
To perform crash-consistency testing on a new application, the only requirement is to provide a workload program that runs operations and generates a data directory, and a checker program that reads the data directory and checks for crash-consistency. This is the same requirement as ALICE.

### Writing a Pathfinder config file

See `targets/leveldb-bug-0/pathfinder-config.ini` for an example.

This is not a true INI file, but rather it is the file format parsed by boost commandline
arguments. Each field can then be templated by Jinja2. Fields in `{{ field }}` are
filled in with associated template values. The following are provided:

- `build_root`: the build directory
- `pwd`: the location of the config file in the build directory


## Common Problems
### Too many files opened
**Error:**
```./pathfinder-core``` failed becasue of too many file opened

**Solution:**
Change the limit of file descriptors by ```ulimit -n 4096```

### `libb64` not found
**Error:**
`libb64` not found

**Solution:**
Change root address in `libb64`, rebuild the directory, and remember to delete the posix_trace.so 
### `pip` Not Found


**Error:**
```bash
sudo: pip: command not found
```

**Solution:**
```bash
sudo apt install python3-pip
```



### Missing Zlib Target in CMake

**Error:**
```
CMake Error at pathfinder/CMakeLists.txt:41 (add_executable):
  Target "pathfinder-core" links to target "ZLIB::ZLIB" but the target was
  not found.  Perhaps a find_package() call is missing for an IMPORTED
  target, or an ALIAS target is missing?
```

**Solution:**
Add the following to your `CMakeLists.txt`:
```cmake
find_package(ZLIB REQUIRED)
```



### Boost Filesystem Error (Missing Path)

**Error:**
```
terminate called after throwing an instance of 'boost::filesystem::filesystem_error'
what(): boost::filesystem::canonical: No such file or directory [generic:2]: "Pathfinder/build/{{YOUR_PATH}}"
Aborted (core dumped)
```

**Solution:**
Configure the `.ini` file in the targets folder before running the program.



### Missing Pin Tool

**Error:**
```
pathfinder::model_checker::model_checker(const pathfinder::trace &, fs::path, chrono::seconds, const pathfinder::persistence_graph *, pathfinder::test_type, pathfinder::pathfinder_mode, bool, bool): could not find pin path "Pathfinder/pin_tool/pin-3.28/pin"
```

**Solution:**
Set up the pin-tool first (i.e run the `setup.sh` in `pin_tool`)



### Bug Files Not Found

**Error:**
```
Error: file Pathfinder/build/../targets/leveldb-bug-0/checker does not exist, required to continue!
```

**Solution:**
Set up the bug files first — `leveldb-bug-0`. Similarily, run the `setup.sh`.



### Uninitialized Variable in GTest

**Error:**
```
Pathfinder/targets/leveldb-bug-0/leveldb/third_party/googletest/googletest/src/gtest-death-test.cc:1301:24: error: ‘dummy’ may be used uninitialized [-Werror=maybe-uninitialized]
  StackLowerThanAddress(&dummy, &result);
```

**Solution:**
Initialize the variable:
```cpp
int dummy = 0;
```


### Deleted `std::atomic` Copy Constructor

**Error:**
```
Pathfinder/targets/leveldb-bug-0/leveldb/benchmarks/db_bench.cc:152:40: error: use of deleted function ‘std::atomic<long unsigned int>::atomic(const std::atomic<long unsigned int>&)’
mutable std::atomic<size_t> count_ = 0;
```

**Solution:**
Use brace-initialization instead:
```cpp
mutable std::atomic<size_t> count_{0};
```

