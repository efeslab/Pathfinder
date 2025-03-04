################################################################################
# Target helpers
################################################################################

function(add_local_pmdk_setup)
    set(options)
    set(oneValue VERSION)
    set(multiValue)
    cmake_parse_arguments(ARGS "${options}" "${oneValue}" "${multiValue}" ${ARGN})

    # this is intentional
    add_checker_executable(USE_LOCAL_PMDK
                           PMDK_VERSION ${ARGS_VERSION}
                           EXTRA_LIBS pmem pmemobj
                           ${ARGS_UNPARSED_ARGUMENTS})
endfunction()