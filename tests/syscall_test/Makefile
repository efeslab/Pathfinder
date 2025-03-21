# SPDX-License-Identifier: BSD-3-Clause
# Copyright 2015-2016, Intel Corporation

#
# examples/libpmemobj/string_store/Makefile -- build the intro 1 example
#
PROGS = writer reader
INCDIR = ../../../pm_examples/write_read/include
LIBDIR = ../../../pm_examples/write_read/debug
INCS = -I$(INCDIR)
LDFLAGS = -Wl,-rpath=$(LIBDIR) -L$(LIBDIR)
LIBS = -lpmemobj -lpmem -pthread

CC = cc
CFLAGS = -std=gnu99 -ggdb -Wall -Werror -Wmissing-prototypes
all: writer reader

writer: writer.o
	$(CC) writer.o -o $@ $(LDFLAGS) $(LIBS)
reader: reader.o
	$(CC) reader.o -o $@ $(LDFLAGS) $(LIBS)
clean:
	rm -f *.o writer reader

%.o: %.c
	$(CC) -c $(CFLAGS) $(INCS) $^