#include <assert.h>

#include "multi_cache_line.h"

void update(foo_t *test, foo_log_t *l, int64_t val) {
    l->old_a = test->a;
    l->old_b = test->b;
    pmem_persist(&l->old_a, sizeof(l->old_a));
    pmem_persist(&l->old_b, sizeof(l->old_b));

    l->in_tx = true;
    pmem_persist(&l->in_tx, sizeof(l->in_tx));

    test->a = val;
    test->b = val;
    pmem_persist(&test->a, sizeof(test->a));
    pmem_persist(&test->b, sizeof(test->b));

    l->in_tx = false;
    pmem_persist(&l->in_tx, sizeof(l->in_tx));
}

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

    foo_t *test = (foo_t*)pm_addr;
    foo_log_t *log = (foo_log_t*)(pm_addr + sizeof(*test));

    assert(test->a == test->b && "started inconsistent!");

    //// --- The actual test

    update(test, log, 7);
    update(test, log, 14);

    //// --- Shutdown
    assert(test->a == test->b && "ended inconsistent!");
    VALGRIND_PMC_REMOVE_PMEM_MAPPING(pm_addr, FLEN);

    munmap(pm_addr, FLEN);
    close(fd);

    return 0;
}