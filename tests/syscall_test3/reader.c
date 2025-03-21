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
    struct file_root temp = { {0} };
    size_t sz = read(fd, &temp, sizeof(temp));
    int write_1, write_2;
    if (sz == sizeof(temp)) {
        write_1 = 1;
        printf("Syscall first read success, buf: %s \n", temp.buf);
        if (strcmp(temp.buf, "write 1")) {
            printf("First write is not 'write 1', inconsistent! \n");
            return 1;
        }
    } 
    else {
        write_1 = 0;
        printf("Syscall first read fail! \n");
    }

    // second read
    struct file_root temp2 = { {0} };
    sz = read(fd, &temp2, sizeof(temp2));
    if (sz == sizeof(temp2)) {
        write_2 = 1;
        printf("Syscall second read success, buf: %s \n", temp2.buf);
        if (strcmp(temp2.buf, "write 2")) {
            printf("Second write is not 'write 2', inconsistent! \n");
            return 1;
        }
    } 
    else {
        write_2 = 0;
        printf("Syscall second read fail! \n");
    }

    // char *fname_pmdk = strcat(fname, "_pmdk");
	PMEMobjpool *pop = pmemobj_open(argv[1], LAYOUT_NAME);
	if (pop == NULL) {
		perror("pmemobj_open");
		return 1;
	}

	PMEMoid root = pmemobj_root(pop, sizeof(struct pmdk_root));
	struct pmdk_root *rootp = pmemobj_direct(root);

	printf("PMDK read success, buf: %s \n", id_2_name[rootp->id]);

    // now do some manual consistency checks
    // if see "B" or "C", first write must happen
    if (rootp->id == B || rootp->id == C) {
        if (!write_1) {
            printf("First write must happen before B or C, inconsistent! \n");
            return 1;
        }
    }

    // if see "D" or "E", second write must happen
    if (rootp->id == D || rootp->id == E) {
        if (!write_2) {
            printf("Second write must happen before D or E, inconsistent! \n");
            return 1;
        }
    }

	pmemobj_close(pop);
    
	if (close(fd) < 0) {
		printf("File close failed! \n");
		return 1;
	}
    




	return 0;
}