cmake_minimum_required(VERSION 3.20)
project(ebtree VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(MSVC)
  set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

list(APPEND CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/cmake")

option(EBTREE_BUILD_TESTS "Build eB-Tree test suites and runner" ON)
option(EBTREE_BUILD_BENCH "Build eB-Tree benchmarks" OFF)
option(EBTREE_BUILD_TOOLS "Build eB-Tree tools (ebtree_audit)" ON)
option(EBTREE_BUILD_SQL "Build SQL layer" ON)
option(EBTREE_RAR_SIGNING "Enable Ed25519 RAR signing in ebtree_audit" OFF)

set(GTEST_CAPI_DIR "${CMAKE_SOURCE_DIR}/gtest_capi" CACHE PATH "gtest_capi root")
set(EBTREE_TEST_SUITES "" CACHE INTERNAL "Registered test suite targets")

set(GTEST_CAPI_BUILD_SAMPLES OFF CACHE BOOL "" FORCE)
set(GTEST_CAPI_MSVC_STATIC_RUNTIME ON CACHE BOOL "" FORCE)
set(GTEST_CAPI_INSTALL OFF CACHE BOOL "" FORCE)

add_subdirectory("${GTEST_CAPI_DIR}" "${CMAKE_BINARY_DIR}/gtest_capi_build")

add_subdirectory(cpp)

if(EBTREE_BUILD_TOOLS)
  add_subdirectory(tools)
endif()

if(EBTREE_BUILD_SQL)
  add_subdirectory(sql)
endif()

if(EBTREE_BUILD_TOOLS AND EBTREE_BUILD_SQL)
  add_subdirectory(demo)
endif()

if(EBTREE_BUILD_BENCH OR EBTREE_BUILD_TESTS)
  add_subdirectory(bench)
endif()

if(EBTREE_BUILD_TESTS)
  enable_testing()
  include(EbtreePython)
  include(EbtreeTestSuite)
  add_subdirectory(test)
endif()
