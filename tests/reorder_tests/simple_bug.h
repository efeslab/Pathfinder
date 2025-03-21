#include <stdlib.h>
#include <sys/mman.h>
#include <unistd.h>
#include <stdio.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <immintrin.h>
#include <sys/mman.h>
#include <unistd.h>
#include <stdint.h>
#include <string.h>

// pmemcheck stuff: https://pmem.io/2015/07/17/pmemcheck-basic.html
#include <valgrind/pmemcheck.h>
#include <libpmem.h>

typedef struct foo {
    int64_t a;
    int64_t b;
    // iangneal: llvm-10 bug is renaming this struct to struct.timestamp
    char _unused;
} foo_t;

#define FLEN (4096lu)
#define MMAP_HINT (void*)(0x80000000)