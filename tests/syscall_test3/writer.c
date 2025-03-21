// a reordering test for mmap+open hybrid file operation
// different from syscall_test2, try to get around with type crawler issue
// A | WRITE B C WRITE D E | F
#include <stdio.h>
// #include <stdlib.h>
#include <string.h>
#include <libpmemobj.h>
#include <libpmem.h>
#include <fcntl.h>
#include <unistd.h>
#include "layout.h"

int pmdk_init(PMEMobjpool *pop, struct pmdk_root *ptr);
int pmdk_store(PMEMobjpool *pop, struct pmdk_root *ptr, BUF_ID id, int persist);
int file_write(int fd, const char* input);

int pmdk_init(PMEMobjpool *pop, struct pmdk_root *ptr) {
	ptr->id = INIT;
	pmemobj_persist(pop, &ptr->id, sizeof(ptr->id));
	printf("PMDK root init success! \n");
	return 0;
}

int pmdk_store(PMEMobjpool *pop, struct pmdk_root *ptr, BUF_ID id, int persist) {
	
	ptr->id = id;
	if (persist) {

		pmemobj_persist(pop, &ptr->id, sizeof(ptr->id));
		printf("PMDK persistent store %s success! \n", id_2_name[id]);
	} else {
		printf("PMDK impersistent store %s success! \n", id_2_name[id]);
	}
	

	return 0;
}

int file_write(int fd, const char* input) {
	if (strlen(input) >= MAX_BUF_LEN) {
		printf("Input length too long, max length is %d! \n", MAX_BUF_LEN);
		return 1;
	}
	struct file_root temp;
	strcpy(temp.buf, input);
    int sz = write(fd, &temp, sizeof(temp));
    if (sz == sizeof(temp)) {
        printf("Syscall write %s success! \n", temp.buf);
		return 0;
    }
    else {
        printf("Syscall write fails! \n");
        return 1;
    }
}

int
main(int argc, char *argv[])
{
	if (argc != 3) {
		printf("usage: %s file-name-pmdk file-name-open\n", argv[0]);
		return 1;
	}
    // char *fname = strdup(argv[1]);

	// syscall file
    // char *fname_open = strcat(fname, "_open");
    int fd = open(argv[2], O_WRONLY | O_CREAT, 0666);
    printf("Syscall file open as fd %d \n", fd);


	// pmfile
    // char *fname_pmdk = strcat(fname, "_pmdk");
	PMEMobjpool *pop = pmemobj_create(argv[1], LAYOUT_NAME,
				PMEMOBJ_MIN_POOL, 0666);

	if (pop == NULL) {
		perror("pmemobj_create");
		return 1;
	}

	PMEMoid root = pmemobj_root(pop, sizeof(struct pmdk_root));
	struct pmdk_root *rootp = pmemobj_direct(root);
	// give squint a hint on type
	// rootp->hint = 0;

	// do initialize
	pmdk_init(pop, rootp);

	// store "A"
	if (pmdk_store(pop, rootp, A, 1)) return 1;

	// file write "write 1"
	if (file_write(fd, "write 1")) return 1;

	// store "B" but not persisted
	if (pmdk_store(pop, rootp, B, 0)) return 1;

	// store "C" but not persisted
	if (pmdk_store(pop, rootp, C, 0)) return 1;

	// file write "write 2"
	if (file_write(fd, "write 2")) return 1;

	// store "D" but not persisted
	if (pmdk_store(pop, rootp, D, 0)) return 1;

	// store "E"
	if (pmdk_store(pop, rootp, E, 1)) return 1;

	// store "F"
	if (pmdk_store(pop, rootp, F, 1)) return 1;

	// fini
	pmemobj_close(pop);

	if (close(fd) < 0) {
		printf("File close failed! \n");
		return 1;
	}


	return 0;
}