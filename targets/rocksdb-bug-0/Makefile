CC = g++
CFLAGS = -std=c++17 -Wall -I/usr/local/include -g
LDFLAGS = -L/usr/local/lib -lpthread -lsnappy -lgflags -lz -lbz2 -llz4 -lzstd -lgcov
ROCKSDB_LIB_PATH ={{ ROCKSDB_LIB_PATH }}

all: workload checker

workload: workload.cpp
	$(CC) $(CFLAGS) workload.cpp -o workload $(ROCKSDB_LIB_PATH)/librocksdb_debug.a $(LDFLAGS) -I$(ROCKSDB_LIB_PATH)/include

checker: checker.cpp
	$(CC) $(CFLAGS) checker.cpp -o checker $(ROCKSDB_LIB_PATH)/librocksdb_debug.a $(LDFLAGS) -I$(ROCKSDB_LIB_PATH)/include

clean:
	rm -f workload checker
