cmake_minimum_required(VERSION 3.10)
project(libtsndgm)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Set default build type and compile flags
if(NOT CMAKE_BUILD_TYPE)
  message(STATUS "CMAKE_BUILD_TYPE set to Release")
  set(CMAKE_BUILD_TYPE Release)
  set(CMAKE_CXX_FLAGS_RELEASE "-O2")
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

add_subdirectory(src)
add_subdirectory(benchmarks)
add_subdirectory(tests)
