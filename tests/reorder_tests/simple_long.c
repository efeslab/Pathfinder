#include "simple_bug.h"

int main(int argc, const char *argv[]) {
    if (argc < 2) {
        fprintf(stderr, "Usage: %s pm_file\n", argv[0]);
        return 1;
    }

    int fd = open(argv[1], O_CREAT | O_RDWR, 0666);
    if (fd < 0) {
        perror(argv[1]);
        return 1;
    }

    // Ensure it's the right size.
    int r = ftruncate(fd, FLEN);
    if (r != 0) {
        perror("ftruncate");
        return 1;
    }

    // Mapping
    void *pm_addr = mmap(MMAP_HINT, FLEN, PROT_READ | PROT_WRITE, 
                         MAP_PRIVATE | MAP_FIXED, fd, 0);
    
    if (pm_addr == MAP_FAILED) {
        perror("mmap");
        return 1;
    }

    VALGRIND_PMC_REGISTER_PMEM_FILE(fd, pm_addr, FLEN, 0);
    VALGRIND_PMC_REGISTER_PMEM_MAPPING(pm_addr, FLEN);

    foo_t *foo_base = (foo_t*)pm_addr;
    foo_t *test_a = foo_base;
    foo_t *test_b = foo_base + 1;
    
    // printf("%p %p\n", test_a, test_b);

    //// --- The actual test

    // Reset, test prioritization of smaller pattern
    test_a->a = 0;
    pmem_persist(&test_a->a, sizeof(test_a->a));
    // pmem_memset_persist(pm_addr + 1024, 0, 16);
    test_a->b = 0;
    pmem_persist(&test_a->b, sizeof(test_a->b));

    test_b->a = 0;
    pmem_persist(&test_a->a, sizeof(test_a->a));
    // pmem_memset_persist(pm_addr + 1024, 0, 16);
    test_b->b = 0;
    pmem_persist(&test_a->b, sizeof(test_a->b));

    // Pattern 1
    test_a->a = 1;
    pmem_persist(&test_a->a, sizeof(test_a->a));
    test_a->b = 2;
    pmem_persist(&test_a->b, sizeof(test_a->b));

    // Pattern 2
    test_b->b = 2;
    pmem_persist(&test_b->b, sizeof(test_b->b));
    test_b->a = 1;
    pmem_persist(&test_b->a, sizeof(test_b->a));

    //// --- Shutdown

    munmap(pm_addr, FLEN);
    close(fd);

    return 0;
}