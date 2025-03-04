################################################################################
# Target helpers
################################################################################

function(add_local_pmdk_checker)
    set(options)
    set(oneValue VERSION)
    set(multiValue)
    cmake_parse_arguments(ARGS "${options}" "${oneValue}" "${multiValue}" ${ARGN})

    add_checker_executable(USE_LOCAL_PMDK
                           PMDK_VERSION ${ARGS_VERSION}
                           EXTRA_LIBS pmem pmemobj
                           ${ARGS_UNPARSED_ARGUMENTS})
endfunction()