#include <stdlib.h>
#include <sys/mman.h>
#include <unistd.h>
#include <stdio.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <immintrin.h>
#include <stdint.h>

// pmemcheck stuff: https://pmem.io/2015/07/17/pmemcheck-basic.html
#include <valgrind/pmemcheck.h>
#include <libpmem.h>


typedef struct entry {                     
    uint8_t array[2];
} entry_t;


int main(void) {
    entry_t test_a, test_b;

    printf("%lu\n", (uint64_t)&test_a);
    printf("%lu\n", (uint64_t)&test_b);

    VALGRIND_PMC_REGISTER_PMEM_MAPPING(&test_a, sizeof (test_a));
    VALGRIND_PMC_REGISTER_PMEM_MAPPING(&test_b, sizeof (test_b));

    // Pattern 1
    test_a.array[0] = 0;
    pmem_persist(&test_a.array[0], sizeof(test_a.array[0]));
    test_a.array[1] = 1;
    pmem_persist(&test_a.array[1], sizeof(test_a.array[1]));

    // Pattern 2
    test_b.array[0] = 0;
    pmem_persist(&test_b.array[0], sizeof(test_b.array[0]));
    test_b.array[1] = 1;
    pmem_persist(&test_b.array[1], sizeof(test_b.array[1]));
    
    VALGRIND_PMC_REMOVE_PMEM_MAPPING(&test_a, sizeof(test_a));
    VALGRIND_PMC_REMOVE_PMEM_MAPPING(&test_b, sizeof(test_b));

    return 0;
}