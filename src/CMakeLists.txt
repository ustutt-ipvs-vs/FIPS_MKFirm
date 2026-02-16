add_subdirectory(network)
add_subdirectory(dgm)
add_subdirectory(heuristic)

add_library(tsndgm INTERFACE)
target_link_libraries(tsndgm INTERFACE network dgm heuristic)
