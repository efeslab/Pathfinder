
function(get_pmdk_vars)
    set(options)
    set(oneValueArgs VERSION LIBRARY_VAR INCLUDE_VAR TARGET_NAME)
    set(multiValueArgs)
    cmake_parse_arguments(ARGS "${options}" "${oneValueArgs}" "${multiValueArgs}" ${ARGN})

    if (NOT DEFINED ARGS_VERSION OR "${ARGS_VERSION}" STREQUAL "")
        set(ARGS_VERSION "1.8")
    endif()

    if (NOT DEFINED PMDK_INSTALL_DIR_${ARGS_VERSION})
        message(FATAL_ERROR "PMDK_INSTALL_DIR_${ARGS_VERSION} not defined!")
    endif()

    set(${ARGS_INCLUDE_VAR} ${PMDK_INCLUDE_DIR_${ARGS_VERSION}} PARENT_SCOPE)
    set(${ARGS_LIBRARY_VAR} ${PMDK_LIBRARY_DIR_${ARGS_VERSION}} PARENT_SCOPE)

    if (DEFINED ARGS_TARGET_NAME)
        set(${ARGS_TARGET_NAME} PMDK_${ARGS_VERSION} PARENT_SCOPE)
    endif()

endfunction()