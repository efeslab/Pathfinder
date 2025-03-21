install(PROGRAMS squint-read DESTINATION bin)
configure_file(squint-read "${CMAKE_BINARY_DIR}/squint-read" COPYONLY)

install(PROGRAMS squint-read DESTINATION bin)
configure_file(run-eval.in "${CMAKE_BINARY_DIR}/run-eval" @ONLY)
configure_file(graph-eval.in "${CMAKE_BINARY_DIR}/graph-eval" @ONLY)
file(CHMOD "${CMAKE_BINARY_DIR}/run-eval"
    PERMISSIONS
        OWNER_READ OWNER_WRITE OWNER_EXECUTE
        GROUP_READ GROUP_WRITE GROUP_EXECUTE
        WORLD_READ WORLD_WRITE WORLD_EXECUTE)
file(CHMOD "${CMAKE_BINARY_DIR}/graph-eval"
    PERMISSIONS
        OWNER_READ OWNER_WRITE OWNER_EXECUTE
        GROUP_READ GROUP_WRITE GROUP_EXECUTE
        WORLD_READ WORLD_WRITE WORLD_EXECUTE)