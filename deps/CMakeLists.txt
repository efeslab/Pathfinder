################################################################################
# Setup
################################################################################

include(ProcessorCount)
ProcessorCount(NPROC)

################################################################################
# PMDK
################################################################################

set(PMDK_ROOT_DIR ${CMAKE_CURRENT_SOURCE_DIR}/pmdk CACHE INTERNAL "")
set(JAARU_PMDK_ROOT_DIR ${CMAKE_CURRENT_SOURCE_DIR}/jaaru_pmdk CACHE INTERNAL "")

# This is jaaru's version
# build_jaaru_pmdk(SOURCE_DIR ${JAARU_PMDK_ROOT_DIR} VERSION "JAARU")
#build_pmdk_version(SOURCE_DIR ${PMDK_ROOT_DIR} VERSION "1.9")

build_pmdk_version(SOURCE_DIR ${PMDK_ROOT_DIR} VERSION "1.8")

# Build this version last for pmreorder's sake
build_pmdk_version(SOURCE_DIR ${PMDK_ROOT_DIR} VERSION "1.9.2" DEPS "PMDK_1.8")


################################################################################
# pmemcheck
################################################################################

set(PMCHK_ROOT_DIR ${CMAKE_CURRENT_SOURCE_DIR}/pmemcheck CACHE INTERNAL "")
set(PMCHK_INSTALL_DIR ${CMAKE_CURRENT_BINARY_DIR}/pmemcheck CACHE INTERNAL "")
set(PMCHK_INCLUDE_DIR ${PMCHK_INSTALL_DIR}/include CACHE INTERNAL "")
set(PMCHK_INCLUDE ${PMCHK_INCLUDE_DIR} CACHE INTERNAL "")
set(PMCHK_LIB_DIR ${PMCHK_INSTALL_DIR}/lib CACHE INTERNAL "")
set(PMCHK_BIN_DIR ${PMCHK_INSTALL_DIR}/bin CACHE INTERNAL
    "Location of pmemcheck binary.")
set(PMCHK_EXE ${PMCHK_BIN_DIR}/valgrind CACHE INTERNAL "Path to pmemcheck binary")

add_custom_command(OUTPUT ${PMCHK_ROOT_DIR}/configure
                   COMMAND ./autogen.sh
                   WORKING_DIRECTORY ${PMCHK_ROOT_DIR}
                   COMMENT "PMEMCheck autogen.sh")
add_custom_command(OUTPUT ${PMCHK_ROOT_DIR}/Makefile
                   DEPENDS ${PMCHK_ROOT_DIR}/configure
                   COMMAND ./configure --prefix=${PMCHK_INSTALL_DIR}
                   WORKING_DIRECTORY ${PMCHK_ROOT_DIR}
                   COMMENT "PMEMCheck configure")
add_custom_command(OUTPUT ${PMCHK_ROOT_DIR}/pmemcheck/pmemcheck-amd64-linux
                   DEPENDS ${PMCHK_ROOT_DIR}/Makefile
                   COMMAND make -C ${PMCHK_ROOT_DIR} -j${NPROC} clean
                   COMMAND make -C ${PMCHK_ROOT_DIR} -j${NPROC}
                   COMMENT "PMEMCheck build")
add_custom_command(OUTPUT ${PMCHK_INSTALL_DIR}/bin/valgrind
                   DEPENDS ${PMCHK_ROOT_DIR}/pmemcheck/pmemcheck-amd64-linux
                   COMMAND make -C ${PMCHK_ROOT_DIR} -j${NPROC} install
                   COMMENT "PMEMCheck install")
add_custom_target(PMEMCHECK ALL : # no-op command
                  DEPENDS ${PMCHK_INSTALL_DIR}/bin/valgrind
                  COMMENT "PMEMCheck dependencies complete")

################################################################################
# Jinja2 C++ library
################################################################################

add_subdirectory(jinja2cpp)
set(JINJA2CPP_INCLUDE_DIRS ${CMAKE_CURRENT_SOURCE_DIR}/jinja2cp/include CACHE INTERNAL "")
set(JINJA2CPP_LIBRARY_DIR ${CMAKE_CURRENT_BINARY_DIR}/jinja2cpp/libjinja2cpp.a CACHE INTERNAL "")
set(JINJA2CPP_LIBRARIES jinja2cpp CACHE INTERNAL "")

################################################################################
# libb64 library
################################################################################
set(LIBB64_INCLUDE_DIRS ${CMAKE_CURRENT_SOURCE_DIR}/libb64/include CACHE INTERNAL "")
# build libb64 as a static library
add_custom_command(OUTPUT ${CMAKE_CURRENT_SOURCE_DIR}/libb64/src/libb64.a
                   COMMAND make -C ${CMAKE_CURRENT_SOURCE_DIR}/libb64/src
                   WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/libb64
                   COMMENT "libb64 build")
add_custom_target(LIBB64 ALL : # no-op command
                    DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/libb64/src/libb64.a
                    COMMENT "libb64 dependencies complete")
set(LIBB64_LIBRARY_DIR ${CMAKE_CURRENT_SOURCE_DIR}/libb64/src CACHE INTERNAL "")
