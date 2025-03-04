cmake_minimum_required(VERSION 3.16)
# Don't enable any languages until we set the compiler environment stuff.
project(Pathfinder LANGUAGES NONE)
set(PATHFINDER_VERSION_MAJOR 3)
set(PATHFINDER_VERSION_MINOR 0)


find_package(LLVM 13 REQUIRED CONFIG)
message(STATUS "Found LLVM ${LLVM_PACKAGE_VERSION}")
message(STATUS "Using LLVMConfig.cmake in: ${LLVM_DIR}")

# https://cmake.org/cmake/help/v3.7/command/set.html#set-environment-variable
# message(STATUS "Setting required compiler environment variables (LLVM_COMPILER=clang; LLVM_COMPILER_PATH=${LLVM_TOOLS_BINARY_DIR}).")
set(LLVM_COMPILER "clang")
set(LLVM_COMPILER_PATH "${LLVM_TOOLS_BINARY_DIR}")

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# https://gitlab.kitware.com/cmake/community/-/wikis/FAQ#how-do-i-use-a-different-compiler
# Set defaults
find_program(WLLVM wllvm)
if(WLLVM STREQUAL "WLLVM-NOTFOUND")
    message(FATAL_ERROR "Could not find wllvm, required for building test programs!")
endif()

find_program(WLLVMXX wllvm++)
if(WLLVM STREQUAL "WLLVMXX-NOTFOUND")
    message(FATAL_ERROR "Could not find wllvm++, required for building test programs!")
endif()

find_program(EXTRACT_BC extract-bc)
if(EXTRACT_BC STREQUAL "EXTRACT_BC-NOTFOUND")
    message(FATAL_ERROR "Could not find extract-bc, required for getting the bitcode from test programs!")
endif()

find_program(ENV_PROG env)
if(ENV_PROG STREQUAL "ENV_PROG-NOTFOUND")
    message(FATAL_ERROR "Could not find env, required for running wllvm/wllvm++!")
endif()

set(WLLVM_PREFIX ${ENV_PROG} LLVM_COMPILER=${LLVM_COMPILER} LLVM_COMPILER_PATH=${LLVM_COMPILER_PATH} CC=${WLLVM} CXX=${WLLVMXX})

# We'll use these variables later for compiling test cases
set(LLVM_LINK "${LLVM_TOOLS_BINARY_DIR}/llvm-link")
set(CLANG_PATH "${LLVM_TOOLS_BINARY_DIR}/clang")
set(CLANGXX_PATH "${LLVM_TOOLS_BINARY_DIR}/clang++")

# https://cmake.org/pipermail/cmake/2009-November/033133.html
# Apparently directly setting CMAKE_C_COMPILER after setting the project is bad.
message(STATUS "Setting CMAKE_C_COMPILER to clang from current setting of ${CMAKE_C_COMPILER}")
set(CMAKE_C_COMPILER "${CLANG_PATH}" CACHE FILEPATH "C compiler for tools (not for test programs)")

message(STATUS "Setting CMAKE_CXX_COMPILER to clang++ from current setting of ${CMAKE_CXX_COMPILER}")
set(CMAKE_CXX_COMPILER "${CLANGXX_PATH}" CACHE FILEPATH "C++ compiler for tools (not for test programs)")

message(STATUS "CMAKE_C_COMPILER=${CMAKE_C_COMPILER}")
message(STATUS "CMAKE_CXX_COMPILER=${CMAKE_CXX_COMPILER}")

enable_language(C CXX)

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -stdlib=libstdc++") 
include_directories("/usr/include/c++/11", "/usr/include/x86_64-linux-gnu/c++/11")

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE "Release" CACHE STRING
      "Choose the type of build, options are: Debug Release RelWithDebInfo MinSizeRel. (Default=Release)"
      FORCE)
endif(NOT CMAKE_BUILD_TYPE)

if(NOT CMAKE_BUILD_TYPE)
    message(STATUS "[${CMAKE_CURRENT_SOURCE_DIR}] No build type selected, default to RelWithDbgInfo")
    set(CMAKE_BUILD_TYPE RelWithDbgInfo)
endif()

# Automatically includes all functions defined in directory.
add_subdirectory(cmake)

add_subdirectory(deps)
add_subdirectory(pathfinder)
add_subdirectory(tests)
add_subdirectory(tools)

# All the benchmarks
# add_subdirectory(targets)
