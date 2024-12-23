if(CMAKE_BUILD_TYPE STREQUAL "Debug")
  set(CMAKE_CXX_CLANG_TIDY "clang-tidy;--config-file=${PROJECT_SOURCE_DIR}/src/.clang-tidy;-fix")
endif()

add_subdirectory(network)
add_subdirectory(dgm)
add_subdirectory(solver)

add_library(tsndgm INTERFACE)
target_link_libraries(tsndgm INTERFACE network dgm solver)
