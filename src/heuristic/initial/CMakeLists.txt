add_library(transmission_ordering STATIC transmission_ordering.cc)
target_include_directories(transmission_ordering PRIVATE ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(transmission_ordering PUBLIC dgm)

add_library(aggregation STATIC aggregation.cc)
target_include_directories(aggregation PRIVATE ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(aggregation PUBLIC dgm transmission_ordering)

add_library(merge STATIC merge.cc)
target_include_directories(merge PRIVATE ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(merge PUBLIC dgm)

add_library(initial STATIC initial.cc)
target_include_directories(initial PRIVATE ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(initial INTERFACE transmission_ordering aggregation merge)
