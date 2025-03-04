# Path to LevelDB source
LEVELDB_SRC = leveldb

# Executables to build
EXECUTABLES = workload checker init

# Common dependencies
DEPENDENCIES = common.h Makefile

# Compiler settings
COMPILER = $(CXX)
CFLAGS = -g -O0 -I/usr/include/ --coverage -std=c++11

# Standard library include paths
# STD_LIB_INCLUDE_PATHS = -I/usr/include/c++/9 \
#                         -I/usr/include/x86_64-linux-gnu/c++/9 \
#                         -I/usr/include/c++/11 \
#                         -I/usr/include/x86_64-linux-gnu/c++/11 \
#                         -I/usr/lib/llvm-14/lib/clang/14.0.0/include \
#                         -I/usr/local/include \
#                         -I/usr/include/x86_64-linux-gnu \
#                         -I/usr/include

# # Export environment variables
# export CC=$(COMPILER)
# export CXX=$(COMPILER)

# Build all executables
all: $(EXECUTABLES)

# Build individual executable
%: %.cc $(LEVELDB_SRC) $(DEPENDENCIES)
	$(COMPILER) $(CFLAGS) -o $@ $< \
		$(LEVELDB_SRC)/build/libleveldb.a \
		-I$(LEVELDB_SRC)/include \
		$(STD_LIB_INCLUDE_PATHS) \
		-lpthread  -lgcov -lc

# Clean up generated files
clean:
	rm -f $(EXECUTABLES)
	rm -f $(EXECUTABLES:%=%.bc)
