#pragma once

extern "C" {

#include "config.h"

// Bigger addresses with more zeros seem to not work
#define PMEM_MMAP_HINT_ADDRSTR "0xd0000000000"

}

#define BOOST_ENABLE_ASSERT_HANDLER 1

// this is an assumption that the block size is 4KB, modify if necessary
#define BLOCK_SIZE 4096

#include <boost/assert.hpp>