cmake_minimum_required(VERSION 3.19)

find_package(Boost 1.74.0 REQUIRED COMPONENTS
    graph iostreams program_options filesystem)

find_package(ZLIB REQUIRED)
# find_package(Curses REQUIRED) # For Terminfo, though this might be optional based on your needs
# find_package(FFI REQUIRED) # This might not be directly available via CMake's find_package, and could need custom handling

# Find mlpack package
find_package(mlpack REQUIRED)

include_directories(
    ${LLVM_INCLUDE_DIRS}
    ${CMAKE_CURRENT_BINARY_DIR}
    ${Boost_INCLUDE_DIRS}
    ${JINJA2CPP_INCLUDE_DIRS}
    ${LIBB64_INCLUDE_DIRS}
    ${MLPACK_INCLUDE_DIRS}
)
link_directories(${LIBB64_LIBRARY_DIR})

add_definitions(${LLVM_DEFINITIONS})

configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/utils/config.h.in"
    "${CMAKE_CURRENT_BINARY_DIR}/config.h"
)

execute_process(
    COMMAND cp ${CMAKE_SOURCE_DIR}/deps/boost_patches/operations.cpp ${CMAKE_BINARY_DIR}/_deps/boost-src/libs/filesystem/src/operations.cpp
    RESULT_VARIABLE COPY_RESULT
)

if(COPY_RESULT EQUAL 0)
    message(STATUS "Successfully replaced Boost filesystem operations.cpp.")
else()
    message(WARNING "Failed to replace Boost filesystem operations.cpp.")
endif()

add_executable(pathfinder-core
    boost_support/gzip.cpp
    utils/file_utils.cpp
    utils/util.cpp
    main.cpp
    model_checker/model_checker.cpp
    model_checker/model_checker_state.cpp
    graph/persistence_graph.cpp
    graph/pm_graph.cpp
    graph/posix_graph.cpp
    runtime/pathfinder_engine.cpp
    runtime/pathfinder_fs.cpp
    runtime/stack_tree.cpp
    trace/stack_frame.cpp
    trace/trace.cpp
    trace/trace_event.cpp
    type_crawler/type_crawler.cpp
    type_crawler/type_info.cpp
)

add_dependencies(pathfinder-core jinja2cpp PMEMCHECK LIBB64)

# if enabled DCMAKE_BUILD_TYPE=Debug
message(STATUS "CMAKE_BUILD_TYPE is '${CMAKE_BUILD_TYPE}'")
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    message("Compiling pathfinder-core in fstandalone-debug mode")
    target_compile_options(pathfinder-core PUBLIC -std=c++17 -O0 -g -fstandalone-debug -fsanitize=address -fsanitize=undefined -fsanitize=leak -fno-omit-frame-pointer)
    target_link_options(pathfinder-core PUBLIC -Wl,--export-dynamic -fsanitize=address -fsanitize=undefined -fsanitize=leak -fno-omit-frame-pointer)
    set(DEBUG_MODE ON)
else()
    message("Compiling pathfinder-core in release mode")
    target_compile_options(pathfinder-core PUBLIC -std=c++17 -O3)
    target_link_options(pathfinder-core PUBLIC -Wl,--export-dynamic)
endif()

target_link_directories(pathfinder-core PUBLIC ${JINJA2CPP_LIBRARY_DIR} ${LLVM_LIBRARY_DIRS} ${Boost_LIBRARY_DIRS} ${ZLIB_LIBRARY_DIRS})

# Find the libraries that correspond to the LLVM components
# that we wish to use
llvm_map_components_to_libnames(llvm_libs support core irreader)

# Link against LLVM libraries
target_link_libraries(pathfinder-core PUBLIC 
    ${llvm_libs} ${Boost_LIBRARIES} ${JINJA2CPP_LIBRARIES} ${ZLIB_LIBRARIES} b64 ${MLPACK_LIBRARIES}
    -Wl,-rpath=${Boost_LIBRARY_DIRS})
