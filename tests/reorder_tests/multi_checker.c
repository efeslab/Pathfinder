#include "multi_cache_line.h"

void recover(foo_t *test, foo_log_t *l) {
    if (!l->in_tx) return;

    test->a = l->old_a;
    test->b = l->old_b;
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

    foo_t *test = (foo_t*)pm_addr;
    foo_log_t *log = (foo_log_t*)(pm_addr + sizeof(*test));

    recover(test, log);

    //// --- The actual test
    if (test->b != test->a) {
        return 1;
    }

    //// --- Shutdown
    VALGRIND_PMC_REMOVE_PMEM_MAPPING(pm_addr, FLEN);


    munmap(pm_addr, FLEN);
    close(fd);

    return 0;
}