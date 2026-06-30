cmake_minimum_required(VERSION 3.20)

add_executable(ebtree_write_bench write_throughput_bench.cc)
target_link_libraries(ebtree_write_bench PRIVATE ebtree_core)

add_executable(ebtree_read_bench read_latency_bench.cc)
target_link_libraries(ebtree_read_bench PRIVATE ebtree_core)

add_executable(ebtree_scan_bench scan_range_bench.cc)
target_link_libraries(ebtree_scan_bench PRIVATE ebtree_core)

add_executable(ebtree_multishard_write_bench multishard_write_bench.cc)
target_link_libraries(ebtree_multishard_write_bench PRIVATE ebtree_core)

add_executable(ebtree_multishard_scan_bench multishard_scan_bench.cc)
target_link_libraries(ebtree_multishard_scan_bench PRIVATE ebtree_core)

add_executable(ebtree_perf_matrix_bench perf_matrix_bench.cc)
target_link_libraries(ebtree_perf_matrix_bench PRIVATE ebtree_core)

if(MSVC)
  target_compile_options(ebtree_write_bench PRIVATE /W4 /utf-8)
  target_link_options(ebtree_write_bench PRIVATE $<$<CONFIG:Release>:/LTCG>)
  target_compile_options(ebtree_read_bench PRIVATE /W4 /utf-8)
  target_compile_options(ebtree_scan_bench PRIVATE /W4 /utf-8)
  target_compile_options(ebtree_multishard_write_bench PRIVATE /W4 /utf-8)
  target_compile_options(ebtree_multishard_scan_bench PRIVATE /W4 /utf-8)
  target_compile_options(ebtree_perf_matrix_bench PRIVATE /W4 /utf-8)
endif()
