#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>
#include <assert.h>
#include <errno.h>
#include <emmintrin.h>
#include <immintrin.h>

#include "layout.h"

int main(int argc, char *argv[]) {

    if (argc < 2) {
        fprintf(stderr, "usage: %s path\n", argv[0]);
        return 1;
    }

    if (!pmemobj_check(argv[1], POBJ_LAYOUT_NAME(driver))) {
        fprintf(stderr, "Pool inconsistent, recreate\n");
        goto pass;
    }

    PMEMobjpool *pop = pmemobj_open(argv[1], POBJ_LAYOUT_NAME(driver));

    if (pop == NULL) {
        pop = pmemobj_create(argv[1], POBJ_LAYOUT_NAME(driver), PMEMOBJ_MIN_POOL, 0666);
        if (pop == NULL) {
            fprintf(stderr, "Object pool is null and could not recreate!! (%s)\n", strerror(errno));
            return 1;
        }

        fprintf(stderr, "Object pool is null and could recreate!\n");
        goto pass;
    }

    TOID(driver_root_t) root = POBJ_ROOT(pop, driver_root_t);

    if (TOID_IS_NULL(root) || !TOID_VALID(root) || !D_RO(root)) {
        fprintf(stderr, "%s not properly created\n", POBJ_LAYOUT_NAME(driver));
        goto fail;
    }

    if (D_RO(root)->a != D_RO(root)->b) {
        fprintf(stderr, "a(%d) != b(%d)\n\t%s inconsistent!\n", 
            D_RO(root)->a, D_RO(root)->b, argv[1]);
        goto fail;
    }

pass:   
    pmemobj_close(pop);
    return 0;

fail:   
    pmemobj_close(pop);
    return 1;
}
