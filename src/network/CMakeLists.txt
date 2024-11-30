add_library(topology STATIC topology.cc)
target_link_libraries(topology PUBLIC nlohmann_json::nlohmann_json)

add_library(histogram STATIC histogram.cc)
target_link_libraries(histogram PUBLIC topology nlohmann_json::nlohmann_json)

add_library(network INTERFACE)
target_link_libraries(network INTERFACE topology histogram)
