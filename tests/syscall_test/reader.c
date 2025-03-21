#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <libpmemobj.h>
#include <fcntl.h>
#include <unistd.h>
#include "layout.h"

int
main(int argc, char *argv[])
{
	if (argc != 3) {
		printf("usage: %s file-name-pmdk file-name-open\n", argv[0]);
		return 1;
	}
	
    // char *fname = strdup(argv[1]);

    // char *fname_open = strcat(fname, "_open");
    // printf("File name: %s \n", fname_open);
    int fd = open(argv[2], O_RDONLY);
    if (fd < 0) {
        printf("File open fails! \n");
        return 1;
    }
    struct my_root temp;
    size_t sz = read(fd, &temp, sizeof(temp));
    if (sz == sizeof(temp)) {
        printf("Syscall read success! \n");
        if (temp.len == strlen(temp.buf)) {
            printf("%s\n", temp.buf);
        }
        else {
            printf("inconsistent writer\n");
            return 1;
        }
    } 
    else {
        printf("Syscall read fail! \n");
        return 1;
    }

    // char *fname_pmdk = strcat(fname, "_pmdk");
	PMEMobjpool *pop = pmemobj_open(argv[1], LAYOUT_NAME);
	if (pop == NULL) {
		perror("pmemobj_open");
		return 1;
	}

	PMEMoid root = pmemobj_root(pop, sizeof(struct my_root));
	struct my_root *rootp = pmemobj_direct(root);

	if (rootp->len == strlen(rootp->buf)) {
        printf("PMDK read success! \n");
		printf("%s\n", rootp->buf);
	} 
	else {
		printf("inconsistent writer\n");
		return 1;
	}

	pmemobj_close(pop);

	if (close(fd) < 0) {
		printf("File close failed! \n");
		return 1;
	}

	return 0;
}