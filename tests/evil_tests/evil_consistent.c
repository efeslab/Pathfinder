#include <stdlib.h>
#include <sys/mman.h>
#include <unistd.h>
#include <stdio.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <immintrin.h>
#include <assert.h>
#include <stdint.h>

// pmemcheck stuff: https://pmem.io/2015/07/17/pmemcheck-basic.html
#include <valgrind/pmemcheck.h>
#include <libpmem.h>

typedef struct foo_type {
    uint32_t a, b, c, d;
} foo_t;

typedef struct bar_type {
    uint64_t a, b;
} bar_t;

static_assert(sizeof(bar_t) == sizeof(foo_t), "Not that evil!");

int main(void) {
    char data[sizeof(foo_t)];
    foo_t *foo = (foo_t*)&data;
    bar_t *bar = (bar_t*)&data;

    VALGRIND_PMC_REGISTER_PMEM_MAPPING(&data, sizeof(data));

    // Pattern 1
    foo->a = 1u;
    pmem_persist(&foo->a, sizeof(foo->a));
    foo->b = 2u;
    pmem_persist(&foo->b, sizeof(foo->b));
    foo->c = 3u;
    pmem_persist(&foo->c, sizeof(foo->c));
    foo->d = 4u;
    pmem_persist(&foo->d, sizeof(foo->d));

    // Evil
    bar->b = 1lu;
    pmem_persist(&bar->b, sizeof(bar->b));
    bar->a = 2lu;
    pmem_persist(&bar->a, sizeof(bar->a));

    // Pattern 2
    foo->a = 1u;
    pmem_persist(&foo->a, sizeof(foo->a));
    foo->b = 2u;
    pmem_persist(&foo->b, sizeof(foo->b));
    foo->c = 3u;
    pmem_persist(&foo->c, sizeof(foo->c));
    foo->d = 4u;
    pmem_persist(&foo->d, sizeof(foo->d));

    VALGRIND_PMC_REMOVE_PMEM_MAPPING(&data, sizeof(data));

    return 0;
}