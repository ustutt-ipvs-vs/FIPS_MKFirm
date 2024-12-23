FetchContent_Declare(argparse
    URL https://github.com/p-ranav/argparse/archive/refs/tags/v3.1.tar.gz
)
FetchContent_MakeAvailable(argparse)

set(BENCHMARKS "heuristic.cc")

foreach(source_file ${BENCHMARKS})
  string(REPLACE ".cc" "" benchmark_name ${source_file})
  add_executable("${benchmark_name}_test" ${source_file})
  target_include_directories("${benchmark_name}_test" PRIVATE ${PROJECT_SOURCE_DIR}/src)
  target_link_libraries("${benchmark_name}_test" PUBLIC tsndgm argparse)
  set_target_properties("${benchmark_name}_test" PROPERTIES OUTPUT_NAME ${benchmark_name})
endforeach(source_file ${BENCHMARKS})
