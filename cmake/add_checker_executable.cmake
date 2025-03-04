#===-- add_test_executable.cmake -------------------------------*- CMake -*-===#
#
# Author: Ian Glen Neal
#
# Purpose: Add a new target that is compiled into a native executable and
#          has it's bitcode automatically extracted and copied into the
#          bin/ folder. This is useful for compiling full benchmarks which
#          can be immediately run in KLEE without having to manually invoke
#          'extract-bc' after recompilation. A convenience facility.
#          NOTE: These executables cannot contain calls to klee_* functions,
#          as they do not run on native executables. All symbolics must be
#          set through the POSIX environment emulation interface.
#
# Usage: See nvmbugs/002_simple_symbolic/CMakeLists.txt.
#
#   add_test_executable(TARGET 001_BranchMod SOURCES branch_nvm_mod.c)
#
#===------------------------------------------------------------------------===#


function(add_checker_executable)
    set(options USE_LOCAL_PMDK)
    set(oneValueArgs TARGET PMDK_VERSION)
    set(multiValueArgs SOURCES EXTRA_LIBS EXTRA_LIB_DIRS EXTRA_INCL_DIRS EXTRA_EXE_FLAGS DEPENDS)
    cmake_parse_arguments(ARGS "${options}" "${oneValueArgs}"
                          "${multiValueArgs}" ${ARGN} )

    set(EXE_FLAGS "-g;-O2;${ARGS_EXTRA_EXE_FLAGS}")
    add_executable(${ARGS_TARGET} ${ARGS_SOURCES})

    target_compile_options(${ARGS_TARGET} PUBLIC ${EXE_FLAGS})

    set(DEPENDENCIES ${ARGS_DEPENDS})

    if (ARGS_USE_LOCAL_PMDK)
        set(PMDK_LIB "")
        set(PMDK_INC "")
        get_pmdk_vars(VERSION ${ARGS_PMDK_VERSION} LIBRARY_VAR PMDK_LIB
                      INCLUDE_VAR PMDK_INC TARGET_NAME PMDK_TARGET)

        target_include_directories(${ARGS_TARGET} PUBLIC ${PMDK_INC})
        target_link_directories(${ARGS_TARGET} PUBLIC ${PMDK_LIB})
        set(DEPENDENCIES ${DEPENDENCIES} ${ARGS_DEPENDS})
    endif()

    list(LENGTH DEPENDENCIES N_DEPS)
    if(${N_DEPS} GREATER 0)
        add_dependencies(${ARGS_TARGET} ${ARGS_DEPENDS})
    endif()

    target_include_directories(${ARGS_TARGET}
                              PUBLIC ${ARGS_EXTRA_INCL_DIRS})
    target_link_directories(${ARGS_TARGET} PUBLIC ${ARGS_EXTRA_LIB_DIRS})
    target_link_libraries(${ARGS_TARGET} PUBLIC ${ARGS_EXTRA_LIBS})

endfunction()
