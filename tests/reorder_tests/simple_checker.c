#include "simple_bug.h"

int main(int argc, const char *argv[]) {

    if (argc < 2) {
        fprintf(stderr, "Usage: %s pm_file\n", argv[0]);
        return 1;
    }

    int fd = open(argv[1], O_RDWR, 0666);
    if (fd < 0) {
        perror(argv[1]);
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

    // printf("%s: begin checking (%p, %p)\n", argv[0], test_a, test_b);

    // printf("\ttest_a: {a = %lu, b = %lu}\n", test_a->a, test_a->b);
    // printf("\ttest_b: {a = %lu, b = %lu}\n", test_b->a, test_b->b);

    //// --- The actual test
    if (test_a->b == 2 && test_a->a != 1) {
        return 1;
    }

    if (test_b->b == 2 && test_b->a != 1) {
        return 1;
    }

    //// --- Shutdown

    munmap(pm_addr, FLEN);
    close(fd);

    return 0;
}