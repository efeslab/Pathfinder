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

#define KEY_LEN 16                        // The maximum length of a key
#define VALUE_LEN 15                      // The maximum length of a value
#define ASSOC_NUM 4  

typedef struct entry{                     
    uint8_t key[KEY_LEN];                 // KEY_LEN and VALUE_LEN are defined in log.h
    uint8_t value[VALUE_LEN];
} entry;

typedef struct level_bucket               // A bucket, put the tokens behind the items to ensure the token area is within the last bytes of the cache line 
{
    entry slot[ASSOC_NUM];
    uint32_t token;                       // each bit in the last ASSOC_NUM bits is used to indicate whether its corresponding slot is empty
} level_bucket;    

int main(void) {
    level_bucket test_a, test_b;

    printf("%lu\n", (uint64_t)&test_a);
    printf("%lu\n", (uint64_t)&test_b);

    VALGRIND_PMC_REGISTER_PMEM_MAPPING(&test_a, sizeof (test_a));
    VALGRIND_PMC_REGISTER_PMEM_MAPPING(&test_b, sizeof (test_b));

    // Pattern 1
    test_a.slot[0].key[0] = 2;
    pmem_persist(&test_a.slot[0].key[0], sizeof(test_a.slot[0].key[0]));
    test_a.token = 1;
    pmem_persist(&test_a.token, sizeof(test_a.token));

    // Pattern 1, but with data 2
    test_b.slot[0].key[0] = 2;
    pmem_persist(&test_b.slot[0].key[0], sizeof(test_b.slot[0].key[0]));
    test_b.token = 1;
    pmem_persist(&test_b.token, sizeof(test_b.token));

    // Pattern 2 (different than Pattern 1!)
    test_a.token = 1;
    pmem_persist(&test_a.token, sizeof(test_a.token));
    test_a.slot[0].key[0] = 2;
    pmem_persist(&test_a.slot[0].key[0], sizeof(test_a.slot[0].key[0]));
    

    VALGRIND_PMC_REMOVE_PMEM_MAPPING(&test_a, sizeof(test_a));
    VALGRIND_PMC_REMOVE_PMEM_MAPPING(&test_b, sizeof(test_b));

    return 0;
}