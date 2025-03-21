#include <stdlib.h>
#include <sys/mman.h>
#include <unistd.h>
#include <stdio.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <immintrin.h>

// pmemcheck stuff: https://pmem.io/2015/07/17/pmemcheck-basic.html
#include <valgrind/pmemcheck.h>
#include <libpmem.h>

typedef struct foo {
    int a;
    int b;
} foo_t;

int main(void) {
    foo_t test_a, test_b;

    VALGRIND_PMC_REGISTER_PMEM_MAPPING(&test_a, sizeof (test_a));
    VALGRIND_PMC_REGISTER_PMEM_MAPPING(&test_b, sizeof (test_b));

    // Pattern 1
    test_a.a = 1;
    pmem_persist(&test_a.a, sizeof(test_a.a));
    test_a.b = 2;
    pmem_persist(&test_a.b, sizeof(test_a.b));

    // Pattern 2
    test_b.a = 1;
    pmem_persist(&test_b.a, sizeof(test_b.a));
    test_b.b = 2;
    pmem_persist(&test_b.b, sizeof(test_b.b));

    VALGRIND_PMC_REMOVE_PMEM_MAPPING(&test_a, sizeof(test_a));
    VALGRIND_PMC_REMOVE_PMEM_MAPPING(&test_b, sizeof(test_b));

    return 0;
}