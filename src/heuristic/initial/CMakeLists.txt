add_library(frame_ordering STATIC frame_ordering.cc)
target_include_directories(frame_ordering PRIVATE ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(frame_ordering PUBLIC dgm)

add_library(initial INTERFACE)
target_link_libraries(initial INTERFACE frame_ordering)
