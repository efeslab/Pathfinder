cmake_minimum_required(VERSION 3.1)

add_local_pmdk_test(TARGET 001_Consistent
                    SOURCES consistent.c
                    EXTRA_INCL_DIRS ${PMCHK_INCLUDE_DIR})

add_local_pmdk_test(TARGET 001_Inconsistent
                    SOURCES inconsistent.c
                    EXTRA_INCL_DIRS ${PMCHK_INCLUDE_DIR})

add_local_pmdk_test(TARGET 001_LevelConsistent
                    SOURCES level_consistent.c
                    EXTRA_INCL_DIRS ${PMCHK_INCLUDE_DIR})

add_local_pmdk_test(TARGET 001_LevelInconsistent
                    SOURCES level_inconsistent.c
                    EXTRA_INCL_DIRS ${PMCHK_INCLUDE_DIR})