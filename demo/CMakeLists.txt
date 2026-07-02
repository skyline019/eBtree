cmake_minimum_required(VERSION 3.20)

add_library(ebtree_demo_flows STATIC
  flow_industrial.cc
  flow_medical.cc
  flow_finance.cc
)
target_include_directories(ebtree_demo_flows
  PUBLIC "${CMAKE_SOURCE_DIR}/demo"
  PRIVATE
    "${CMAKE_SOURCE_DIR}/tools/ebtree_audit"
    "${CMAKE_SOURCE_DIR}/cpp/include"
    "${CMAKE_SOURCE_DIR}"
)
target_link_libraries(ebtree_demo_flows PUBLIC ebtree_core ebtree_audit_lib ebtree_sql)

add_executable(ebtree_demo_industrial industrial_main.cc)
target_link_libraries(ebtree_demo_industrial PRIVATE ebtree_demo_flows)

add_executable(ebtree_demo_medical medical_main.cc)
target_link_libraries(ebtree_demo_medical PRIVATE ebtree_demo_flows)

add_executable(ebtree_demo_finance finance_main.cc)
target_link_libraries(ebtree_demo_finance PRIVATE ebtree_demo_flows)

if(MSVC)
  target_compile_options(ebtree_demo_flows PRIVATE /W4 /utf-8)
  target_compile_options(ebtree_demo_industrial PRIVATE /W4 /utf-8)
  target_compile_options(ebtree_demo_medical PRIVATE /W4 /utf-8)
  target_compile_options(ebtree_demo_finance PRIVATE /W4 /utf-8)
endif()

set_target_properties(ebtree_demo_industrial ebtree_demo_medical ebtree_demo_finance
  PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/$<CONFIG>"
)
