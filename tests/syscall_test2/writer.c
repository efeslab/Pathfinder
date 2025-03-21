// a reordering test for mmap+open hybrid file operation
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
int pmdk_store(PMEMobjpool *pop, struct pmdk_root *ptr, const char* input, BUF_ID id, int persist);
int file_write(int fd, const char* input);

int pmdk_init(PMEMobjpool *pop, struct pmdk_root *ptr) {
	char buf[MAX_BUF_LEN] = {0};
	strcpy(buf, "INIT\0");
	pmemobj_memcpy_persist(pop, ptr->buf_A, buf, strlen(buf));
	pmemobj_memcpy_persist(pop, ptr->buf_B, buf, strlen(buf));
	pmemobj_memcpy_persist(pop, ptr->buf_C, buf, strlen(buf));
	pmemobj_memcpy_persist(pop, ptr->buf_D, buf, strlen(buf));
	pmemobj_memcpy_persist(pop, ptr->buf_E, buf, strlen(buf));
	pmemobj_memcpy_persist(pop, ptr->buf_F, buf, strlen(buf));
	printf("PMDK root init success! \n");
	return 0;
}

int pmdk_store(PMEMobjpool *pop, struct pmdk_root *ptr, const char* input, BUF_ID id, int persist) {
	char* target_buf;
	switch (id)
	{
	case A:
		target_buf = ptr->buf_A;
		break;

	case B:
		target_buf = ptr->buf_B;
		break;	

	case C:
		target_buf = ptr->buf_C;
		break;

	case D:
		target_buf = ptr->buf_D;
		break;	

	case E:
		target_buf = ptr->buf_E;
		break;

	case F:
		target_buf = ptr->buf_F;
		break;	

	default:
		printf("Unrecognized buf id! \n");
		return 1;
	}

	if (strlen(input) >= MAX_BUF_LEN) {
		printf("Input length too long, max length is %d! \n", MAX_BUF_LEN);
		return 1;
	}
	// char buf[MAX_BUF_LEN] = {0};
	strcpy(target_buf, input);

	if (persist) {
		// pmemobj_memcpy_persist(pop, target_buf, input, strlen(input));
		pmem_persist(target_buf, strlen(target_buf));
		printf("PMDK persistent store %s success! \n", input);
	} else {
		// memcpy(target_buf, input, strlen(input));
		printf("PMDK impersistent store %s success! \n", input);
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
	if (pmdk_store(pop, rootp, "A", A, 1)) return 1;

	// file write "write 1"
	if (file_write(fd, "write 1")) return 1;

	// store "B" but not persisted
	if (pmdk_store(pop, rootp, "B", B, 0)) return 1;

	// store "C" but not persisted
	if (pmdk_store(pop, rootp, "C", C, 0)) return 1;

	// file write "write 2"
	if (file_write(fd, "write 2")) return 1;

	// store "D" but not persisted
	if (pmdk_store(pop, rootp, "D", D, 0)) return 1;

	// store "E"
	if (pmdk_store(pop, rootp, "E", E, 1)) return 1;

	// store "F"
	if (pmdk_store(pop, rootp, "F", F, 1)) return 1;

	// fini
	pmemobj_close(pop);

	if (close(fd) < 0) {
		printf("File close failed! \n");
		return 1;
	}


	return 0;
}