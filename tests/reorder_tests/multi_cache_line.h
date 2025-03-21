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
#include <stdbool.h>

// pmemcheck stuff: https://pmem.io/2015/07/17/pmemcheck-basic.html
#include <valgrind/pmemcheck.h>
#include <libpmem.h>

typedef struct foo {
    int64_t a;
    int64_t b;
} foo_t;

typedef struct log {
    bool in_tx;
    char _u0[128];
    int64_t old_a;
    char _u1[128];
    int64_t old_b;
} foo_log_t;

#define FLEN (4096lu)
#define MMAP_HINT (void*)(0x80000000)