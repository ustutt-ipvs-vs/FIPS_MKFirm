include(FetchContent)
FetchContent_Declare(
  googletest
  GIT_REPOSITORY https://github.com/google/googletest.git
  GIT_TAG b514bdc898e2951020cbdca1304b75f5950d1f59 # v1.15.2
)
FetchContent_MakeAvailable(googletest)
include(GoogleTest)

set(TESTS "topology.cc" "histogram.cc" "transmission_graph.cc")

foreach(source_file ${TESTS})
  string(REPLACE ".cc" "" test_name ${source_file})
  add_executable("${test_name}_test" ${source_file})
  target_include_directories("${test_name}_test" PRIVATE ${PROJECT_SOURCE_DIR}/src)
  target_link_libraries("${test_name}_test" PUBLIC tsndgm GTest::gtest_main)
  set_target_properties("${test_name}_test" PROPERTIES OUTPUT_NAME ${test_name})
  gtest_discover_tests("${test_name}_test")
endforeach(source_file ${TESTS})
