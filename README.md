# Pathfinder

[![arXiv](https://img.shields.io/badge/arXiv-2503.01390-b31b1b.svg)](https://arxiv.org/abs/2503.01390)
[![DOI](https://img.shields.io/badge/DOI-10.1145/3720431-blue.svg)](https://doi.org/10.1145/3720431)

## Introduction

This version contains necessary code and step to reproduce the main scientific claims in the paper.

Pathfinder is a scalable and accurate application-level crash-consistency tool. It leverages representative testing: a new crash-state space reduction strategy based on the key observation that the consistency of crash states is often correlated, even if those crash states are not identical. Pathfinder supports testing both POSIX-based applications and MMIO-based applications.

There are mainly two kinds of applications evaluated: POSIX-based applications and MMIO-based applications.

For POSIX-based applications, this version contains workload, trace obtained from the workload and evaluation configuration for Pathfinder to reproduce all the bugs detected.
The configuration also supports counting crash states and evaluates our implementation of Persevere by changing parameters.
It additionally contains code coverage data collected from the hardware setup in the paper.

The ALICE tool baseline could be found at [https://github.com/efeslab/alice](https://github.com/efeslab/alice).

For MMIO-based applications, this version contains the full implementation for performing crash-consistency testing. However, we do not include MMIO-based workloads in this version as running them requiring access to machines that are equipped with persistent memory.
*Therefore, the main target of this artifact is to obtain the Artifact Available and Artifact Evaluated badges*.

*We also provide a dedicated server with an environment setup for artifact evaluation. Please see `ARTIFACTS.md` for more details.*

## Hardware Dependencies


For POSIX-based applications, we run our experiments on a server with an Intel Xeon Platinum 8480+ CPU (2.00 GHz). However, any Linux-based machine with Ubuntu 22.04 should generally work.

For MMIO-based applications, we run our experiments on a server with an Intel Xeon Gold 6230 CPU (2.10 GHz) and 4×128GB Intel Optane Series 100 Pmem DIMMs.


## Directory structure

- `cmake`: Contains some custom CMake functions used to build targets.
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

(*For artifact evaluation, if the dedicated server provided by us is used, you can skip this step as we pre-compiled all the applications. For example, `leveldb` application is setup and compiled in `targets/leveldb-bug-0/leveldb`*)

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

`persevere` specifies whether Pathfinder uses its own implementation of Persevere baseline to perform the testing.

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

The core implementation of Pathfinder in `pathfinder` as well as the tracing tool in `pin_tool` should be evaluated for reusability. 

### Testing new applications

To perform crash-consistency testing on a new application, the only requirement is to provide a workload program that runs operations and generates a data directory, and a checker program that reads the data directory and checks for crash-consistency. This is the same requirement as ALICE. The workload program should be compiled in debug mode from scratch so that Pathfinder's tracing tool could obtain the complete backtrace.

Pathfinder currently supports C/C++ workloads. This limitation is due to our Pin tool-based tracer, which records POSIX syscalls, MMIO accesses, and native backtraces for C/C++ binaries. Pathfinder can be extended to other languages that exercise POSIX and MMIO by leveraging an alternative tracer that emits the same events and backtraces for those runtimes. 

Below is an example of testing a new C/C++ workload in Pathfinder.

#### Example
Consider the example in Figure 4 of the paper as a new C/C++ workload we would like to test. We will first prepare the workload program.

The whole example could be found in `Pathfinder/targets/example`.

```
// Pathfinder/targets/example/workload.cpp

...

// Fn2(f) { write(f); write(f); }
static void Fn2(const std::string& f) {
    append_write(f, "Fn2: first line\n");
    append_write(f, "Fn2: second line\n");
}

// Fn4(f1,f2) { write(f1); write(f2); fdatasync(f2); }
static void Fn4(const std::string& f1, const std::string& f2) {
    append_write(f1, "Fn4: write to f1\n");
    // open f2 and keep the fd to call fdatasync on it
    int fd2 = ::open(f2.c_str(), O_CREAT | O_WRONLY | O_APPEND, 0644);
    if (fd2 < 0) terminate(("open " + f2).c_str());
    const char* data = "Fn4: write to f2\n";
    append_write(f2, data);
    if (::fdatasync(fd2) < 0) terminate(("fdatasync " + f2).c_str());
    if (::close(fd2) < 0) terminate(("close " + f2).c_str());
}

// Fn5(f) { rename(f); sync(); }
static void Fn5(const std::string& f) {
    std::string newname = f + ".renamed";
    if (::rename(f.c_str(), newname.c_str()) < 0) terminate(("rename " + f).c_str());
    ::sync();  // flush filesystem metadata to disk
}

// Fn3(f1,f2,rename_flag) { Fn4(f1); if (rename_flag) Fn5(f2); }
static void Fn3(const std::string& f1, const std::string& f2, bool rename_flag) {
    Fn4(f1, f2);
    if (rename_flag) Fn5(f2);
}

// Fn1() {
//   Fn2(f1);
//   Fn3(f1,f2,true);
//   ...
//   Fn2(f7);make 
//   Fn3(f7,f8,false);
// }
static void Fn1(const std::string& dir) {
    std::string f1 = dir + "/f1.txt";
    std::string f2 = dir + "/f2.txt";
    std::string f7 = dir + "/f7.txt";
    std::string f8 = dir + "/f8.txt";

    Fn2(f1);
    Fn3(f1, f2, true);

    // (Ellipsis in the figure — do anything else you want in between)

    Fn2(f7);
    Fn3(f7, f8, false);

    std::cout << "Done. See files under ./" << dir << "\n";
}

...

```

Then we will prepare a checker program. Specifically, this checker checks if it is possible that in function `Fn3`, the rename operation to `f2.txt` is persisted to disk before the writes in `Fn4` are persisted.

```
// Pathfinder/targets/example/checker.cpp
...
    // Check if a renamed f2 file exists
    for (auto& entry : fs::directory_iterator(dir)) {
        if (entry.is_regular_file() &&
            entry.path().filename().string().find("f2.txt") != std::string::npos &&
            entry.path().filename() != "f2.txt") {
            std::cout << "rename_applied: " << rename_applied << std::endl;
            rename_applied = true;
            break;
        }
    }

    // Check if "Fn4: write to f1" is in f1.txt
    bool f1_has_write = false;
    std::ifstream fin(f1);
    if (fin) {
        std::string line;
        while (std::getline(fin, line)) {
            std::cout << "line: " << line << std::endl;
            if (line.find("Fn4: write to f1") != std::string::npos) {
                std::cout << "f1_has_write: " << f1_has_write << std::endl;
                f1_has_write = true;
            }
        }
    }

    // Report result
    if (rename_applied && !f1_has_write) {
        std::cout << "[POSSIBLE ANOMALY] rename persisted but f1 write did not.\n";
        return 1; // anomaly
    } else {
        std::cout << "[OK] Either rename not applied, or f1 write present.\n";
        return 0; // normal
        
...
```

After compiling the above two programs and making sure they run without runtime errors, we will prepare a Pathfinder config. This config file specifies to use POSIX mode for crash-consistency testing and the commands for workload and checker programs.

```
// Pathfinder/targets/example/pathfinder-config.ini
[general]
output_dir_tmpl = {{ build_root }}/example
verbose = yes
pm_fs_path = ../fs_path
max_nproc = 80
parallelize = yes
sanity_test = no
fsync_test = no
count_crash_state = no
mode = posix

[trace]
verbose = yes
cmd_tmpl = {{ pwd }}/workload {{ pmdir }}

[test]
checker_tmpl = {{ pwd }}/checker {{ pmdir }}
timeout = 30
```

After running this example with `Pathfinder/build/pathfinder/pathfinder-core Pathfinder/targets/example/pathfinder-config.ini`, we will observe failure cases.

```
ret_code,message,note,timestamp(posix mode)
0,1,2,3,4,5,6,9,10,11,12,15,1,2,3,4,5,6,7,8,9,10,11,12,1,"[STDOUT] rename_applied: 0
[STDOUT] line: Fn2: first line
[STDOUT] line: Fn2: second line
[STDOUT] [POSSIBLE ANOMALY] rename persisted but f1 write did not.
","posix",1
```

This shows that a crash-consistency bug is triggered when operations #7 and #8 are not applied. Upon checking the tracer log, we can verify that these operations correspond to the write to `f1.txt` issued by `Fn4`. 

```
6,0,OPEN,Pathfinder/fs_path/31ea-df44-82a7-8a7a/f1.txt,1089,420,6,33;__open,,,0x00011453b;Fn4,Pathfinder/targets/example/workload.cpp,39,0x000001a28;main,Pathfinder/targets/example/workload.cpp,58,0x000001588;__libc_init_first,,,0x000029d90;__libc_start_main,,,0x000029e40;_start,,,0x000001735;
7,0,WRITE,6,Pathfinder/fs_path/31ea-df44-82a7-8a7a/f1.txt,17,Rm40OiB3cml0ZSB0byBmMQo=;__write,,,0x000114887;append_write,Pathfinder/targets/example/workload.cpp,23,0x000001876;Fn4,Pathfinder/targets/example/workload.cpp,39,0x000001a28;main,Pathfinder/targets/example/workload.cpp,58,0x000001588;__libc_init_first,,,0x000029d90;__libc_start_main,,,0x000029e40;_start,,,0x000001735;
8,0,CLOSE,6,Pathfinder/fs_path/31ea-df44-82a7-8a7a/f1.txt;__close,,,0x000114f67;append_write,Pathfinder/targets/example/workload.cpp,28,0x00000188e;Fn4,Pathfinder/targets/example/workload.cpp,39,0x000001a28;main,Pathfinder/targets/example/workload.cpp,58,0x000001588;__libc_init_first,,,0x000029d90;__libc_start_main,,,0x000029e40;_start,,,0x000001735;
9,0,OPEN,Pathfinder/fs_path/31ea-df44-82a7-8a7a/f2.txt,1089,420,6,0;__open,,,0x00011453b;main,Pathfinder/targets/example/workload.cpp,58,0x000001588;__libc_init_first,,,0x000029d90;__libc_start_main,,,0x000029e40;_start,,,0x000001735;
```


### Writing a Pathfinder config file

See `targets/leveldb-bug-0/pathfinder-config.ini` for an example.

This is not a true INI file, but rather it is the file format parsed by boost commandline
arguments. Each field can then be templated by Jinja2. Fields in `{{ field }}` are
filled in with associated template values. The following are provided:

- `build_root`: the build directory
- `pwd`: the location of the config file in the build directory


### More documentations on Pathfinder

For a complete list of features supported by Pathfinder, please refer to the driver program `pathfinder/main.cpp`.

### Limitations

Pathfinder currently supports testing POSIX-based and MMIO-based applications.
For applications that use both syscalls and MMIOs, Pathfinder can detect crash-consistency bugs from either syscall-level reordering, or memory operation-level reordering, but not both. 
To detect a bug arised from interactions between syscalls and MMIOs, the Pathfinder persistence graph defined in `pathfinder/graph` needs to be subclassed to represent both syscalls and MMIOs in a single graph.

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

