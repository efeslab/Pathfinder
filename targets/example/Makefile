# Simple Makefile for posix_persistence_demo
CXX      = g++
CXXFLAGS = -std=c++17 -Wall -Wextra -O2 -g

all: workload checker

workload: workload.cpp
	$(CXX) $(CXXFLAGS) $< -o $@

checker: checker.cpp
	$(CXX) $(CXXFLAGS) $< -o $@

clean:
	rm -f workload checker
