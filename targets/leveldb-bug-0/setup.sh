#!/bin/bash

# Setup LevelDB
git clone https://github.com/google/leveldb.git
cd leveldb
git checkout 8cce47e450b365347769959c53b8836ef0216df
git submodule init
git submodule update
mkdir build
cd build
cmake -DCMAKE_C_FLAGS="--coverage" -DCMAKE_CXX_FLAGS="--coverage" -DCMAKE_BUILD_TYPE=Debug ..
make -j


# Compile workload
cd ../../
export CC=gcc
export CXX=g++
export CFLAGS="-g -O0 -I/usr/include/ -I/usr/lib/llvm-13/include"
export CXXFLAGS="-g -O0 -I/usr/include/ -I/usr/lib/llvm-13/include"

make clean
make -j