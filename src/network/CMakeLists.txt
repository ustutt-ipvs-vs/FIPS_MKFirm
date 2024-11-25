add_library(network STATIC topology.cc)
target_link_libraries(network PUBLIC nlohmann_json::nlohmann_json)
