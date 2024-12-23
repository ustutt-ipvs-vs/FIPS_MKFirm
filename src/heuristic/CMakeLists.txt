add_subdirectory(initial)

add_library(heuristic INTERFACE)
target_link_libraries(heuristic INTERFACE initial)
