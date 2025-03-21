#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>
#include <assert.h>
#include <errno.h>
#include <emmintrin.h>
#include <immintrin.h>
#include <unistd.h>
#include <sys/types.h>

#include <valgrind/pmemcheck.h>
#include "layout.h"

int main(int argc, char *argv[]) {

    if (argc < 2) {
        fprintf(stderr, "usage: %s path\n", argv[0]);
        return 1;
    }

    // (void*)pop is actually the mmap-ed region...
    PMEMobjpool *pop = pmemobj_create(argv[1], POBJ_LAYOUT_NAME(driver), PMEMOBJ_MIN_POOL, 0666);
    if (pop == NULL) {
        fprintf(stderr, "Create pool %s failed (%s)\n", POBJ_LAYOUT_NAME(driver), strerror(errno));
        pop = pmemobj_open(argv[1], POBJ_LAYOUT_NAME(driver));
    }

    if (pop == NULL) {
        fprintf(stderr, "Object pool is null! (%s)\n", strerror(errno));
        return 1;
    }

    TOID(driver_root_t) root = POBJ_ROOT(pop, driver_root_t);

    assert(!TOID_IS_NULL(root) && "Root is null!");
    assert(TOID_VALID(root) && "Root is invalid!");
    assert(D_RO(root) && "Root direct is null!");

    assert(D_RO(root)->a == D_RO(root)->b && "starting out inconsistent!");

    VALGRIND_PMC_EMIT_LOG("VALIDATION.BEGIN");
    TX_BEGIN(pop) {
        TX_ADD(root);
        // pmem_persist((void*)pop + 4096, PMEMOBJ_MIN_POOL - 4096);
        // VALGRIND_PMC_EMIT_LOG("GLOBAL_FLUSH");
        // VALGRIND_PMC_EMIT_LOG("FENCE");
        // pmem_persist(D_RW(root), sizeof(*D_RW(root)));
        D_RW(root)->a = 1;
        pmem_persist(D_RW(root), sizeof(*D_RW(root)));
        VALGRIND_PMC_EMIT_LOG("VALIDATION.END");

        D_RW(root)->b = 1;
        // pmem_persist((void*)pop + 4096, PMEMOBJ_MIN_POOL - 4096);
    } TX_END
    
    // TX_BEGIN(pop) {
    //     TX_SET(root, a, 2);
    //     TX_SET(root, b, 2);
    // } TX_END


    assert(D_RO(root)->a == D_RO(root)->b && "ending inconsistent!");
    
    pmemobj_close(pop);

    return 0;
}
