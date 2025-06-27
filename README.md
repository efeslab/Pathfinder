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
TODO

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

