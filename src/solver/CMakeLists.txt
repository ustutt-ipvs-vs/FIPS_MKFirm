add_library(initial STATIC initial.cc)
target_link_libraries(initial PUBLIC dgm)

add_library(solver INTERFACE)
target_link_libraries(solver INTERFACE initial)
