#!/bin/bash

# Setup LevelDB
git clone https://github.com/efeslab/leveldb.git
cd leveldb
git checkout 19a10cf5cb9bc27c0cfe54929e2fed703b1d7c37
make -j


# Compile workload
cd ..
make -j