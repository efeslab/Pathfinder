#===-- build_pmdk_version.cmake --------------------------------*- CMake -*-===#
#
# Author: Ian Glen Neal
#
# Purpose: TODO
#
# Usage: TODO
#
#===------------------------------------------------------------------------===#
function(build_pmdk_version)
    set(options)
    set(oneValueArgs VERSION SOURCE_DIR)
    set(multiValueArgs DEPS)
    cmake_parse_arguments(ARGS "${options}" "${oneValueArgs}" "${multiValueArgs}" ${ARGN})

    if (NOT DEFINED ARGS_VERSION)
        set(ARGS_VERSION "1.8")
    endif()

    # Locals for build simplicity
    set(INSTALL_DIR ${CMAKE_CURRENT_BINARY_DIR}/pmdk_${ARGS_VERSION})
    set(INCLUDE_DIR ${INSTALL_DIR}/include)
    set(LIBRARY_DIR ${INSTALL_DIR}/lib/pmdk_debug)

    set(PMDK_INSTALL_DIR_${ARGS_VERSION} ${INSTALL_DIR}
        CACHE INTERNAL "PMDK (version ${ARGS_VERSION}) install directory")
    set(PMDK_INCLUDE_DIR_${ARGS_VERSION} ${INCLUDE_DIR}
        CACHE INTERNAL "PMDK (version ${ARGS_VERSION}) include directory")
    set(PMDK_LIBRARY_DIR_${ARGS_VERSION} ${LIBRARY_DIR}
        CACHE INTERNAL "PMDK (version ${ARGS_VERSION}) library directory")

    # set(INSTALL_DIR_${ARGS_VERSION} ${CMAKE_CURRENT_BINARY_DIR}/pmdk_${ARGS_VERSION})
    # message(FATAL_ERROR "'${ARGS_VERSION}' ${INSTALL_DIR} should equal ${INSTALL_DIR_master} should equal ${INSTALL_DIR_${ARGS_VERSION}}")
    # message(FATAL_ERROR "PMDK_INSTALL_DIR_${ARGS_VERSION} = ${PMDK_INSTALL_DIR_${ARGS_VERSION}}")

    # For some reason, you HAVE to do this via a variable or it thinks you're setting
    # VALGRIND_ENABLED to "1 -Wno...."
    set(PMDK_FLAGS "-DVALGRIND_ENABLED=1 -Wno-error=unused-function")
    if (ARGS_VERSION VERSION_LESS_EQUAL "1.0")
        set(PMDK_FLAGS "${PMDK_FLAGS} -Wno-error=shorten-64-to-32")
    endif()

    set(ENV_CMD ${ENV_PROG} LLVM_COMPILER=${LLVM_COMPILER} LLVM_COMPILER_PATH=${LLVM_COMPILER_PATH})

    set(CC_VARS
        CC=${WLLVM}
        CXX=${WLLVMXX})

    add_custom_command(OUTPUT
        ${LIBRARY_DIR}/libpmemobj.so
        ${LIBRARY_DIR}/libpmem.so
                        COMMAND git -C ${ARGS_SOURCE_DIR} checkout ${ARGS_VERSION}
                        COMMAND ${ENV_CMD} make ${CC_VARS} -C ${ARGS_SOURCE_DIR} clobber
                        COMMAND ${ENV_CMD} make ${CC_VARS} -C ${ARGS_SOURCE_DIR} clean
                        COMMAND ${ENV_CMD} make ${CC_VARS}
                                DEBUG=1
                                EXTRA_CFLAGS=${PMDK_FLAGS}
                                    -C ${ARGS_SOURCE_DIR} -j${NPROC}
                        COMMAND ${ENV_CMD} make ${CC_VARS}
                                DEBUG=1
                                EXTRA_CFLAGS=${PMDK_FLAGS}
                                -C ${ARGS_SOURCE_DIR} -j${NPROC}
                                install prefix=${INSTALL_DIR}
                        COMMAND ${ENV_CMD} ${CC_VARS}
                                ${EXTRACT_BC} ${LIBRARY_DIR}/libpmem.so
                        COMMAND ${ENV_CMD} ${CC_VARS}
                                ${EXTRACT_BC} ${LIBRARY_DIR}/libpmemobj.so
                        DEPENDS ${ARGS_SOURCE_DIR} ${ARGS_DEPS}
                        COMMENT "Build and install PMDK (version ${ARGS_VERSION})")
    add_custom_target(PMDK_${ARGS_VERSION} : # no-op command
                        DEPENDS ${LIBRARY_DIR}/libpmemobj.so ${LIBRARY_DIR}/libpmem.so
                        COMMENT "PMDK version ${ARGS_VERSION} dependencies complete")

endfunction()

function(build_jaaru_pmdk)
    set(options)
    set(oneValueArgs VERSION SOURCE_DIR)
    set(multiValueArgs DEPS)
    cmake_parse_arguments(ARGS "${options}" "${oneValueArgs}" "${multiValueArgs}" ${ARGN})

    # Locals for build simplicity
    set(INSTALL_DIR ${CMAKE_CURRENT_BINARY_DIR}/pmdk_${ARGS_VERSION})
    set(INCLUDE_DIR ${INSTALL_DIR}/include)
    set(LIBRARY_DIR ${INSTALL_DIR}/lib/nvml_debug)

    set(PMDK_INSTALL_DIR_${ARGS_VERSION} ${INSTALL_DIR}
        CACHE INTERNAL "PMDK (version ${ARGS_VERSION}) install directory")
    set(PMDK_INCLUDE_DIR_${ARGS_VERSION} ${INCLUDE_DIR}
        CACHE INTERNAL "PMDK (version ${ARGS_VERSION}) include directory")
    set(PMDK_LIBRARY_DIR_${ARGS_VERSION} ${LIBRARY_DIR}
        CACHE INTERNAL "PMDK (version ${ARGS_VERSION}) library directory")

    # set(INSTALL_DIR_${ARGS_VERSION} ${CMAKE_CURRENT_BINARY_DIR}/pmdk_${ARGS_VERSION})
    # message(FATAL_ERROR "'${ARGS_VERSION}' ${INSTALL_DIR} should equal ${INSTALL_DIR_master} should equal ${INSTALL_DIR_${ARGS_VERSION}}")
    # message(FATAL_ERROR "PMDK_INSTALL_DIR_${ARGS_VERSION} = ${PMDK_INSTALL_DIR_${ARGS_VERSION}}")

    # For some reason, you HAVE to do this via a variable or it thinks you're setting
    # VALGRIND_ENABLED to "1 -Wno...."
    set(PMDK_FLAGS "-DVALGRIND_ENABLED=1 ")

    set(ENV_CMD ${ENV_PROG} LLVM_COMPILER=${LLVM_COMPILER} LLVM_COMPILER_PATH=${LLVM_COMPILER_PATH})

    set(CC_VARS
        CC=${WLLVM}
        CXX=${WLLVMXX})

    set(MAKE_PMDK ${ENV_CMD} make ${CC_VARS} EXTRA_CFLAGS=${PMDK_FLAGS} -C ${ARGS_SOURCE_DIR} -j${NPROC})
    message(STATUS "MAKE_PMDK command: ${MAKE_PMDK}")
    add_custom_command(OUTPUT
        ${LIBRARY_DIR}/libpmemobj.so
        ${LIBRARY_DIR}/libpmem.so
                        COMMAND ${MAKE_PMDK}
                        COMMAND ${ENV_CMD} make ${CC_VARS}
                                EXTRA_CFLAGS=${PMDK_FLAGS}
                                -C ${ARGS_SOURCE_DIR} -j${NPROC}
                                install prefix=${INSTALL_DIR}
                        COMMAND ${ENV_CMD} ${CC_VARS}
                                ${EXTRACT_BC} ${LIBRARY_DIR}/libpmem.so
                        COMMAND ${ENV_CMD} ${CC_VARS}
                                ${EXTRACT_BC} ${LIBRARY_DIR}/libpmemobj.so
                        DEPENDS ${ARGS_SOURCE_DIR} ${ARGS_DEPS}
                        COMMENT "Build and install PMDK (version ${ARGS_VERSION})")
    add_custom_target(PMDK_${ARGS_VERSION} : # no-op command
                        DEPENDS ${LIBRARY_DIR}/libpmemobj.so ${LIBRARY_DIR}/libpmem.so
                        COMMENT "PMDK version ${ARGS_VERSION} dependencies complete")

endfunction()


