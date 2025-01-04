add_library(transmission_graph STATIC transmission_graph.cc transmission_operations.cc)
target_include_directories(transmission_graph PRIVATE ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(transmission_graph PUBLIC network)

add_library(transmission_graph_merger STATIC transmission_graph_merger.cc)
target_include_directories(transmission_graph_merger PRIVATE ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(transmission_graph_merger PUBLIC transmission_graph)

add_library(traversal STATIC traversal.cc)
target_include_directories(traversal PRIVATE ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(traversal PUBLIC network)

add_library(critical_path STATIC critical_path.cc)
target_include_directories(critical_path PRIVATE ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(critical_path PUBLIC traversal)

add_library(dgm INTERFACE)
target_link_libraries(dgm INTERFACE transmission_graph transmission_graph_merger traversal critical_path)
