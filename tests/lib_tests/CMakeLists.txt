cmake_minimum_required(VERSION 3.1)

add_local_pmdk_test(TARGET TxConsistent SOURCES consistent.c VERSION "1.8")
add_local_pmdk_checker(TARGET TxConsistent.checker SOURCES checker.c VERSION "1.8")

add_local_pmdk_test(TARGET TxInconsistent SOURCES inconsistent.c)
add_local_pmdk_checker(TARGET TxInconsistent.checker SOURCES checker.c)