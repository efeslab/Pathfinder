#!/bin/bash

# Setup LevelDB
git clone https://github.com/efeslab/leveldb.git
cd leveldb
git checkout 607c20951808c204d8abe33fe6d7d958467dd19f
make -j


# Compile workload
cd ..
make -j