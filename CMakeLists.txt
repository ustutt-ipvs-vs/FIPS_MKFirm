cmake_minimum_required(VERSION 3.10)
project(libtsndgm)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Set default build type and compile flags
if(NOT CMAKE_BUILD_TYPE)
  message(STATUS "CMAKE_BUILD_TYPE set to Release")
  set(CMAKE_BUILD_TYPE Release)
  set(CMAKE_CXX_FLAGS_RELEASE "-O3 -march=native -pg -g3 -ggdb")
elseif(CMAKE_BUILD_TYPE STREQUAL "Debug")
  message(STATUS "CMAKE_BUILD_TYPE set to Debug")
  set(CMAKE_CXX_FLAGS_DEBUG "-g -Wall")
  add_compile_options(-fsanitize=address)
  add_link_options(-fsanitize=address)
endif()

# Fetch content
cmake_policy(SET CMP0135 NEW)
include(FetchContent)
FetchContent_Declare(json 
  URL https://github.com/nlohmann/json/releases/download/v3.11.3/json.tar.xz
  URL_HASH SHA256=d6c65aca6b1ed68e7a182f4757257b107ae403032760ed6ef121c9d55e81757d
)
FetchContent_MakeAvailable(json)

# Temporary solution to get generator similar to std::generator
set(CMAKE_MESSAGE_LOG_LEVEL "ERROR") # CMP0135 is not set in this repo, suppress warning
FetchContent_Declare(generator 
  GIT_REPOSITORY https://github.com/TartanLlama/generator.git
  GIT_TAG 2a912502de4f97dcba4f95c958ee0ddf7bc22cf5
)
FetchContent_MakeAvailable(generator)
set(CMAKE_MESSAGE_LOG_LEVEL "WARNING")

add_subdirectory(src)
add_subdirectory(tests)
