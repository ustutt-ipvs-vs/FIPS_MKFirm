FetchContent_Declare(argparse
    URL https://github.com/p-ranav/argparse/archive/refs/tags/v3.1.tar.gz
)
FetchContent_MakeAvailable(argparse)

add_executable(heuristic heuristic.cc)
target_include_directories(heuristic PRIVATE "../src")
target_link_libraries(heuristic PUBLIC tsndgm argparse)
