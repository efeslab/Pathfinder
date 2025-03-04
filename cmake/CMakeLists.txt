file(GLOB CMAKE_FUNCTIONS *.cmake)

foreach(CMAKE_FILE IN ITEMS ${CMAKE_FUNCTIONS})
    include(${CMAKE_FILE})
endforeach()
