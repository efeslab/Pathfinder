cmake_minimum_required(VERSION 3.1)

set(SETUP_SRC writer.c)

set(DRIVER_SRC writer.c)

set(CHECKER_SRC reader.c)

add_local_pmdk_test(TARGET syscall_test2 SOURCES ${DRIVER_SRC} VERSION "1.8")
add_local_pmdk_checker(TARGET syscall_test2.checker SOURCES ${CHECKER_SRC} VERSION "1.8")

configure_file(squint-config.ini "${CMAKE_CURRENT_BINARY_DIR}/squint-config.ini" COPYONLY)