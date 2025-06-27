#---------------------------------------------------------
# Makefile to compile two C++ programs (workload and checker)
# that use the WiredTiger library.
#
# Adjust the following variables as needed:
#   WT_INCLUDE: path to WiredTiger headers
#   WT_LIB:     path to WiredTiger library
#   CXX:        your C++ compiler
#   CXXFLAGS:   additional compiler flags
#---------------------------------------------------------

# Path to WiredTiger headers (e.g., /usr/local/include)
WT_INCLUDE ={{ WIREDTIGER_PATH }}/build/include

# Path to WiredTiger library (e.g., /usr/local/lib)
WT_LIB ={{ WIREDTIGER_PATH }}/build

# Compiler
CXX = g++

# Compiler flags
CXXFLAGS = -std=c++17 -I$(WT_INCLUDE) -g -O0

# Linker flags (link with WiredTiger)
LDFLAGS =  -lgcov

# Programs to build
PROGRAMS = workload checker

all: $(PROGRAMS)

# Rule to build "workload" from "workload.o"
workload: workload.o
	$(CXX) $(CXXFLAGS) $^ $(WT_LIB)/libwiredtiger.a -o $@ $(LDFLAGS)

# Rule to build "checker" from "checker.o"
checker: checker.o
	$(CXX) $(CXXFLAGS) $^ $(WT_LIB)/libwiredtiger.a -o $@ $(LDFLAGS)

# Generic rule to build object files from C++ sources
%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

# Clean rule
clean:
	rm -f *.o $(PROGRAMS)

.PHONY: all clean
