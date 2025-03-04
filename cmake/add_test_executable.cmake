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


function(add_test_executable)
    set(options USE_LOCAL_PMDK)
    set(oneValueArgs TARGET PMDK_VERSION)
    set(multiValueArgs
        SOURCES EXTRA_LIBS EXTRA_BITCODE_LIBS EXTRA_LIB_DIRS
        EXTRA_INCL_DIRS EXTRA_EXE_FLAGS DEPENDS)
    cmake_parse_arguments(ARGS
        "${options}" "${oneValueArgs}" "${multiValueArgs}" ${ARGN})

    get_property(CURR_INCLUDE_DIRS DIRECTORY PROPERTY INCLUDE_DIRECTORIES)
    # message(FATAL_ERROR "INCLUDE at ${CMAKE_CURRENT_SOURCE_DIR} IS '${CURR_INCLUDE_DIRS}'")

    set(ARGS_EXTRA_INCL_DIRS ${ARGS_EXTRA_INCL_DIRS} ${CURR_INCLUDE_DIRS})
    # if(${CMAKE_CURRENT_SOURCE_DIR} MATCHES "pmdk_btree_jaaru")
    #     message(FATAL_ERROR "INCLUDE at ${CMAKE_CURRENT_SOURCE_DIR} IS |${CURR_INCLUDE_DIRS}|, or |${ARGS_EXTRA_INCL_DIRS}|")
    # endif()

    # set(EXE_FLAGS "-g;-O0;-Xclang;-disable-llvm-passes;${ARGS_EXTRA_EXE_FLAGS}")
    set(EXE_FLAGS -g -O0 -D__NO_STRING_INLINES -D_FORTIFY_SOURCE=0 -U__OPTIMIZE__)
    # add_executable(${ARGS_TARGET} ${ARGS_SOURCES})

    # message(FATAL_ERROR "${EXE_FLAGS}")

    # target_compile_options(${ARGS_TARGET} PUBLIC ${EXE_FLAGS})

    # if (ARGS_USE_LOCAL_PMDK)
    #     set(PMDK_LIB "")
    #     set(PMDK_INC "")
    #     get_pmdk_vars(VERSION ${ARGS_PMDK_VERSION} LIBRARY_VAR PMDK_LIB
    #                   INCLUDE_VAR PMDK_INC TARGET_NAME PMDK_TARGET)

    #     # target_include_directories(${ARGS_TARGET} PUBLIC ${PMDK_INC})
    #     # target_link_directories(${ARGS_TARGET} PUBLIC ${PMDK_LIB})
    #     # add_dependencies(${ARGS_TARGET} ${PMDK_TARGET})
    # endif()

    # target_include_directories(${ARGS_TARGET}
    #                           PUBLIC ${ARGS_EXTRA_INCL_DIRS})
    # target_link_directories(${ARGS_TARGET} PUBLIC ${ARGS_EXTRA_LIB_DIRS})
    # target_link_libraries(${ARGS_TARGET} PUBLIC ${ARGS_EXTRA_LIBS})

    # Manually compile so we can use wllvm
    set(COMPILER ${WLLVM})
    foreach(F IN ITEMS ${ARGS_SOURCES})
        get_filename_component(F_EXT ${F} EXT)
        if(F_EXT STREQUAL ".cpp" OR F_EXT STREQUAL ".cxx"
           OR F_EXT STREQUAL ".hpp" OR F_EXT STREQUAL ".hxx" )
            set(COMPILER ${WLLVMXX})
            break()
        endif()
    endforeach()

    if(COMPILER STREQUAL ${WLLVM})
        set(EXE_FLAGS "${EXE_FLAGS} ${CMAKE_C_FLAGS}")
    else()
        set(EXE_FLAGS "${EXE_FLAGS} ${CMAKE_CXX_FLAGS}")
    endif()

    # message(FATAL_ERROR ${EXE_FLAGS})

    set(OUTFILE "${CMAKE_CURRENT_BINARY_DIR}/${ARGS_TARGET}")
    set(DEPENDENCIES ${PMEMCHECK} ${ARGS_DEPENDS})

    if (ARGS_USE_LOCAL_PMDK)
        set(PMDK_LIB "")
        set(PMDK_INC "")
        get_pmdk_vars(VERSION ${ARGS_PMDK_VERSION} LIBRARY_VAR PMDK_LIB
                    INCLUDE_VAR PMDK_INC TARGET_NAME PMDK_TARGET)

        # target_include_directories(${ARGS_TARGET} PUBLIC ${PMDK_INC})
        # target_link_directories(${ARGS_TARGET} PUBLIC ${PMDK_LIB})
        # add_dependencies(${ARGS_TARGET} ${PMDK_TARGET})

        set(ARGS_EXTRA_INCL_DIRS ${PMDK_INC} ${ARGS_EXTRA_INCL_DIRS})
        set(ARGS_EXTRA_LIB_DIRS ${PMDK_LIB} ${ARGS_EXTRA_LIB_DIRS})
        set(DEPENDENCIES ${DEPENDENCIES} ${PMDK_TARGET})
    endif()

    set(COMPILER_ARGS "")
    foreach(INCL_DIR IN ITEMS ${ARGS_EXTRA_INCL_DIRS})
        if(IS_ABSOLUTE ${INCL_DIR})
            list(APPEND COMPILER_ARGS "-I${INCL_DIR}")
        else()
            list(APPEND COMPILER_ARGS "-I${CMAKE_CURRENT_SOURCE_DIR}/${INCL_DIR}")
        endif()
    endforeach()
    get_filename_component(PARENT_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/.." ABSOLUTE)
    list(APPEND COMPILER_ARGS
        "-I${CMAKE_CURRENT_SOURCE_DIR}"
        "-I${PARENT_SOURCE_DIR}")
    foreach(SOURCE_FILE IN ITEMS ${ARGS_SOURCES})
        if(IS_ABSOLUTE ${SOURCE_FILE})
            list(APPEND COMPILER_ARGS "${SOURCE_FILE}")
        else()
            list(APPEND COMPILER_ARGS "${CMAKE_CURRENT_SOURCE_DIR}/${SOURCE_FILE}")
        endif()
    endforeach()

    set(PATCHELF_LIBS "")

    foreach(LIB_DIR IN ITEMS ${ARGS_EXTRA_LIB_DIRS})
        if(IS_ABSOLUTE ${LIB_DIR})
            set(ABS_LIB_DIR "${LIB_DIR}")
        else()
            set(ABS_LIB_DIR "${CMAKE_CURRENT_SOURCE_DIR}/${LIB_DIR}")
        endif()

        list(APPEND COMPILER_ARGS "-L${ABS_LIB_DIR}")
        list(APPEND PATCHELF_LIBS "${ABS_LIB_DIR}")
    endforeach()
    foreach(LIB IN ITEMS ${ARGS_EXTRA_LIBS})
        list(APPEND COMPILER_ARGS "-l${LIB}")
    endforeach()

    set(BITCODE_LIBS "")
    foreach(BC IN LISTS ARGS_EXTRA_BITCODE_LIBS)
        list(APPEND BITCODE_LIBS "--override" "${BC}")
    endforeach()

    if (ARGS_USE_LOCAL_PMDK)
        if ("pmem" IN_LIST ARGS_EXTRA_LIBS)
            list(APPEND BITCODE_LIBS "--override" "${PMDK_LIB}/libpmem.so.bc")
            list(APPEND PATCHELF_LIBS "${PMDK_LIB}/libpmem.so")
        endif()

        if ("pmemobj" IN_LIST ARGS_EXTRA_LIBS)
            list(APPEND BITCODE_LIBS "--override" "${PMDK_LIB}/libpmemobj.so.bc")
            list(APPEND PATCHELF_LIBS "${PMDK_LIB}/libpmemobj.so")
        endif()
    endif()

    # iangneal: shell "no-op", effectively
    set(PATCHELF_CMD ":")
    if(PATCHELF_LIBS)
        string(REPLACE ";" " " PATCHELF_LIBS_STR "${PATCHELF_LIBS}")
        set(PATCHELF_CMD "patchelf --set-rpath ${PATCHELF_LIBS_STR} ${OUTFILE}")
    endif()

    add_custom_target(${ARGS_TARGET} ALL
                ${ENV_PROG}
                    LLVM_COMPILER=${LLVM_COMPILER}
                    LLVM_COMPILER_PATH=${LLVM_COMPILER_PATH}
                ${COMPILER}
                -o ${OUTFILE}
                ${COMPILER_ARGS}
                ${EXE_FLAGS} ${ARGS_EXTRA_EXE_FLAGS}
            COMMAND /bin/bash -c "${PATCHELF_CMD}"
            COMMAND
                ${ENV_PROG}
                    LLVM_COMPILER=${LLVM_COMPILER}
                    LLVM_COMPILER_PATH=${LLVM_COMPILER_PATH}
                ${EXTRACT_BC} ${OUTFILE}
            COMMAND mv ${OUTFILE}.bc ${OUTFILE}.unlinked.bc
            COMMAND ${LLVM_LINK} ${OUTFILE}.unlinked.bc
                                ${BITCODE_LIBS}
                                -o ${OUTFILE}.bc
            DEPENDS ${DEPENDENCIES})

    # Now we auto extract
    # add_custom_command(TARGET ${ARGS_TARGET}
    #                   POST_BUILD

    #                   DEPENDS ${OUTFILE})
                    #   COMMAND ${EXTRACT_BC} $<TARGET_FILE:${ARGS_TARGET}>
                    #   DEPENDS $<TARGET_FILE:${ARGS_TARGET}>)

    # Don't do this, causes type information to go missing
    # set(BITCODE_LIBS "--only-needed")


    # add_custom_command(TARGET ${ARGS_TARGET}
    #                   POST_BUILD
    #                   COMMAND mv ${OUTFILE}.bc ${OUTFILE}unlinked.bc
    #                   COMMAND ${LLVM_LINK} ${OUTFILE}.unlinked.bc
    #                                        ${BITCODE_LIBS}
    #                                         -o ${OUTFILE}.bc
    #                   DEPENDS ${OUTFILE}.bc)

    # add_custom_command(TARGET ${ARGS_TARGET}
    #                   POST_BUILD
    #                   COMMAND mv $<TARGET_FILE:${ARGS_TARGET}>.bc
    #                               $<TARGET_FILE:${ARGS_TARGET}>.unlinked.bc
    #                   COMMAND ${LLVM_LINK}
    #                             $<TARGET_FILE:${ARGS_TARGET}>.unlinked.bc
    #                                        ${BITCODE_LIBS}
    #                                         -o $<TARGET_FILE:${ARGS_TARGET}>.bc
    #                   DEPENDS $<TARGET_FILE:${ARGS_TARGET}>.bc)

endfunction()
