add_library(transmission_graph STATIC transmission_graph.cc transmission_operations.cc)
target_link_libraries(transmission_graph PUBLIC network)

add_library(traversal STATIC traversal.cc)
target_link_libraries(traversal PUBLIC network)

add_library(critical_path STATIC critical_path.cc)
target_link_libraries(critical_path PUBLIC traversal)

add_library(dgm INTERFACE)
target_link_libraries(dgm INTERFACE transmission_graph traversal critical_path)
