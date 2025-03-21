cmake_minimum_required(VERSION 3.1)
find_package(PkgConfig REQUIRED)

# Subdirectories can access header files in this directory
include_directories(${CMAKE_CURRENT_SOURCE_DIR})

################################################################################

add_subdirectory(simple_tests)
add_subdirectory(evil_tests)
add_subdirectory(reorder_tests)
add_subdirectory(lib_tests)

add_subdirectory(syscall_test)
add_subdirectory(syscall_test2)
add_subdirectory(syscall_test3)