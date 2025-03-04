#!/bin/bash

# Setup Pin-3.28
wget https://software.intel.com/sites/landingpage/pintool/downloads/pin-3.28-98749-g6643ecee5-gcc-linux.tar.gz
tar -xvzf pin-3.28-98749-g6643ecee5-gcc-linux.tar.gz
mv pin-3.28-98749-g6643ecee5-gcc-linux pin-3.28
cp tool_src/makefile.unix.config pin-3.28/source/tools/Config

# Start to make the tools
cd tool_src
make PIN_ROOT=../pin-3.28 obj-intel64/posix_tracer.so
make PIN_ROOT=../pin-3.28 obj-intel64/posix_read_observer.so
make PIN_ROOT=../pin-3.28 obj-intel64/mmio_read_observer.so