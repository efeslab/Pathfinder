# Pathfinder



## Directory structure

- `cmake`: Contains some custom CMake functions uses to build targets.
- `deps`: Project dependencies, notably PMDK.
- `pathfinder`: Core source code directory
- `targets`: These are the workloads Pathfinder tests. This contains source code and `pathfinder-config.ini` files, which tells Pathfinder how to test the targets.

## Setup instructions

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

## Build instructions

```sh
cd Pathfinder #if you're not there already
mkdir build
cd build
cmake ..
make pathfinder-core -j
```

Note: the make process may generate some warnings (e.g., `WARNING:Did not recognize the compiler flag "-dM"`). This is expected (a consequence of using WLLVM for compilation).

## Finding bugs

The targets in `targets` define a `pathfinder-config.ini` file to define run commands for testing. 

### To find bugs
```sh
cd Pathfinder/build
./pathfinder/pathfinder-core targets/<some target directory>/<some config file>

# example:
./pathfinder/pathfinder-core targets/leveldb-bug-0/pathfinder-config.ini
```

### Writing a Pathfinder config file

See `targets/leveldb-bug-0/pathfinder-config.ini` for an example.

This is not a true INI file, but rather it is the file format parsed by boost commandline
arguments. Each field can then be templated by Jinja2. Fields in `{{ field }}` are
filled in with associated template values. The following are provided:

- `build_root`: the build directory
- `pwd`: the location of the config file in the build directory


## Common Problems
- If ```./pathfinder-core``` failed becasue of too many file opened, change the limit of file descriptors by ```ulimit -n 4096```
- If `libb64` not found, change root address in `libb64`, rebuild the directory, and remember to delete the posix_trace.so 

