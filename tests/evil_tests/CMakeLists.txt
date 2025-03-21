cmake_minimum_required(VERSION 3.1)

add_local_pmdk_test(TARGET 002_EvilConsistent
                    SOURCES evil_consistent.c
                    EXTRA_INCL_DIRS ${PMCHK_INCLUDE_DIR})

add_local_pmdk_test(TARGET 002_EvilInconsistent
                    SOURCES evil_inconsistent.c
                    EXTRA_INCL_DIRS ${PMCHK_INCLUDE_DIR})

add_local_pmdk_test(TARGET 002_ArrayConsistent
                    SOURCES array_consistent.c
                    EXTRA_INCL_DIRS ${PMCHK_INCLUDE_DIR})

add_local_pmdk_test(TARGET 002_ArrayInconsistent
                    SOURCES array_inconsistent.c
                    EXTRA_INCL_DIRS ${PMCHK_INCLUDE_DIR})

add_local_pmdk_test(TARGET 002_LevelConsistent
                    SOURCES level_consistent.c
                    EXTRA_INCL_DIRS ${PMCHK_INCLUDE_DIR})

add_local_pmdk_test(TARGET 002_LevelInconsistent
                    SOURCES level_inconsistent.c
                    EXTRA_INCL_DIRS ${PMCHK_INCLUDE_DIR})