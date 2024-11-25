include(FetchContent)
FetchContent_Declare(
  googletest
  GIT_REPOSITORY https://github.com/google/googletest.git
  GIT_TAG b514bdc898e2951020cbdca1304b75f5950d1f59 # v1.15.2
)
FetchContent_MakeAvailable(googletest)
include(GoogleTest)

add_executable(topology_test topology.cc)
target_link_libraries(topology_test PUBLIC network GTest::gtest_main)

gtest_discover_tests(topology_test)
