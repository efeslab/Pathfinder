cmake_minimum_required(VERSION 3.1)

add_local_pmdk_test(TARGET 000_SimpleShort
                    SOURCES simple_short.c
                    EXTRA_INCL_DIRS ${PMCHK_INCLUDE_DIR})

add_local_pmdk_checker(TARGET 000_SimpleShort.checker
                       SOURCES simple_checker.c
                       EXTRA_INCL_DIRS ${PMCHK_INCLUDE_DIR})

add_local_pmdk_test(TARGET 000_SimpleLong
                    SOURCES simple_long.c
                    EXTRA_INCL_DIRS ${PMCHK_INCLUDE_DIR})

add_local_pmdk_checker(TARGET 000_SimpleLong.checker
                       SOURCES simple_checker.c
                       EXTRA_INCL_DIRS ${PMCHK_INCLUDE_DIR})

add_local_pmdk_test(TARGET MultiCacheLineCorrect
                    SOURCES multi_driver.c
                    EXTRA_INCL_DIRS ${PMCHK_INCLUDE_DIR})

add_local_pmdk_checker(TARGET MultiCacheLineCorrect.checker
                       SOURCES multi_checker.c
                       EXTRA_INCL_DIRS ${PMCHK_INCLUDE_DIR})