add_library(topology STATIC topology.cc)
target_link_libraries(topology PUBLIC nlohmann_json::nlohmann_json tl::generator)

add_library(histogram STATIC histogram.cc)
target_link_libraries(histogram PUBLIC topology nlohmann_json::nlohmann_json)

add_library(stream STATIC stream.cc)
target_link_libraries(stream PUBLIC topology histogram nlohmann_json::nlohmann_json)

add_library(network INTERFACE)
target_link_libraries(network INTERFACE topology histogram stream)
