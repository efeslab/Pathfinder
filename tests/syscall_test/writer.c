// a writer test serving as microbenchmark 
// first create a pmfile via pmdk, then do so using regular fopen
#include <stdio.h>
// #include <stdlib.h>
#include <string.h>
#include <libpmemobj.h>
#include <fcntl.h>
#include <unistd.h>
#include "layout.h"

int
main(int argc, char *argv[])
{
	if (argc != 4) {
		printf("usage: %s file-name-pmdk file-name-open input\n", argv[0]);
		return 1;
	}
    // char *fname = strdup(argv[1]);

	// syscall file
    // char *fname_open = strcat(fname, "_open");
    int fd = open(argv[2], O_WRONLY | O_CREAT, 0666);
    printf("Syscall file open as fd %d \n", fd);

    struct my_root temp;
    strcpy(temp.buf, argv[3]);
    temp.len = strlen(argv[3]);
    int sz = write(fd, &temp, sizeof(temp));
	printf("buf: %s \n", argv[3]);
    if (sz == sizeof(temp)) {
        printf("Syscall write success! \n");
    }
    else {
        printf("Syscall write fails! \n");
        return 1;
    }

	if (close(fd) < 0) {
		printf("File close failed! \n");
		return 1;
	}

	// pmfile
    // char *fname_pmdk = strcat(fname, "_pmdk");
	PMEMobjpool *pop = pmemobj_create(argv[1], LAYOUT_NAME,
				PMEMOBJ_MIN_POOL, 0666);

	if (pop == NULL) {
		perror("pmemobj_create");
		return 1;
	}

	PMEMoid root = pmemobj_root(pop, sizeof(struct my_root));
	struct my_root *rootp = pmemobj_direct(root);
	rootp->len = 0;
	pmemobj_persist(pop, &rootp->len, sizeof(rootp->len));

	if (strlen(argv[3]) > MAX_BUF_LEN) {
		printf("input larger than maximum buffer length! \n");
		return 1;
	}

	char buf[MAX_BUF_LEN] = {0};
	strcpy(buf, argv[3]);
	
	rootp->len = strlen(buf);
	
	pmemobj_memcpy_persist(pop, rootp->buf, buf, rootp->len);
	

	pmemobj_persist(pop, &rootp->len, sizeof(rootp->len));

	// pmemobj_memcpy_persist(pop, rootp->buf, buf, rootp->len);

	pmemobj_close(pop);
    // free(fname);
    printf("PMDK write success! \n");






	return 0;
}