include(EbtreeTestSuite)

set(EBTEST_GENERATED_INC_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated" CACHE INTERNAL "")
file(MAKE_DIRECTORY "${EBTEST_GENERATED_INC_DIR}")

ebtree_add_test_suite(
  NAME unit
  SOURCES
    unit/unit_success_test.cc
    unit/manifest_consistency_test.cc
    unit/sync_manifest_test.cc
    unit/invariant_durability_test.cc
    unit/invariant_manifest_test.cc
    unit/invariant_summary_test.cc
    unit/invariant_recovery_test.cc
    unit/invariant_memtable_test.cc
    unit/invariant_gc_test.cc
    unit/shard_engine_test.cc
    unit/invariant_mmap_test.cc
    unit/invariant_forbidden_test.cc
    unit/invariant_paged_btree_test.cc
    unit/invariant_concurrency_test.cc
    unit/forbidden_manifest_test.cc
    unit/forbidden_enforcement_test.cc
    unit/page_file_test.cc
    unit/wal_key_index_test.cc
    unit/invariant_ondisk_btree_test.cc
    unit/lazy_committed_get_test.cc
    unit/datafile_lsn_index_test.cc
    unit/value_codec_test.cc
    unit/lzma_codec_test.cc
    unit/balanced_durability_test.cc
    unit/recovery_state_test.cc
    unit/read_resolver_test.cc
  CATEGORIES success unit invariant
)

target_compile_definitions(ebtree_tests_unit PRIVATE
  "EBTEST_SYNC_MANIFEST=\"${CMAKE_SOURCE_DIR}/Docs/syncs/SYNC_MANIFEST.yaml\""
  "EBTEST_INVARIANT_MANIFEST=\"${CMAKE_SOURCE_DIR}/Docs/invariants/INVARIANT_MANIFEST.yaml\""
  "EBTEST_TEST_MANIFEST=\"${CMAKE_SOURCE_DIR}/test/TEST_MANIFEST.yaml\""
)

ebtree_add_test_suite(
  NAME failure
  SOURCES
    failure/failure_paths_test.cc
    failure/durability_bounds_test.cc
    failure/lazy_recovery_test.cc
    failure/full_replay_test.cc
    failure/powerfail_test.cc
    failure/chaos_harness_test.cc
    failure/concurrent_chaos_test.cc
    failure/random_powerfail_test.cc
  CATEGORIES failure
)

ebtree_add_test_suite(
  NAME nested
  SOURCES nested/nested_fixture_test.cc
  CATEGORIES nested success
)

ebtree_add_test_suite(
  NAME complex
  SOURCES
    complex/complex_bulk_test.cc
    complex/btree_no_fallback_test.cc
    complex/summary_heal_test.cc
    complex/summary_async_heal_test.cc
    complex/concurrent_read_test.cc
    complex/flashback_test.cc
    complex/multishard_test.cc
    complex/scale_bulk_test.cc
    complex/lazy_shard_test.cc
  CATEGORIES complex success
)

ebtree_add_test_suite(
  NAME composite
  SOURCES composite/composite_recovery_test.cc
  CATEGORIES composite success
)

ebtree_add_test_suite(
  NAME pipeline
  SOURCES
    pipeline/pipeline_durability_test.cc
    pipeline/tlog_durability_test.cc
    pipeline/rto_open_test.cc
    pipeline/gc_region_test.cc
    pipeline/memtable_pipeline_test.cc
    pipeline/mmap_recovery_test.cc
    pipeline/multishard_recovery_test.cc
    pipeline/paged_btree_recovery_test.cc
    pipeline/background_flush_test.cc
    pipeline/lazy_get_test.cc
    pipeline/perf_regression_test.cc
    pipeline/compress_datafile_test.cc
    pipeline/compress_perf_test.cc
  CATEGORIES pipeline success
)

ebtree_add_test_suite(
  NAME matrix
  SOURCES
    matrix/durability_matrix_test.cc
    matrix/recovery_matrix_test.cc
    matrix/no_fallback_matrix_test.cc
    matrix/pipeline_matrix_test.cc
    matrix/shard_matrix_test.cc
    matrix/chaos_matrix_test.cc
    matrix/paged_matrix_test.cc
    matrix/concurrent_matrix_test.cc
    matrix/flashback_matrix_test.cc
    matrix/balanced_matrix_test.cc
    matrix/ondisk_matrix_test.cc
    matrix/wal_index_matrix_test.cc
  CATEGORIES matrix semantic
)

ebtree_add_test_suite(
  NAME audit
  SOURCES
    audit/rar_physical_test.cc
    audit/rar_recovery_test.cc
    audit/rar_oracle_equivalence_test.cc
    audit/rar_op_log_equivalence_test.cc
    audit/rar_multishard_oplog_test.cc
    audit/rar_multishard_inferred_test.cc
    audit/rar_group_oplog_test.cc
    audit/rar_schema_conformance_test.cc
    audit/rar_policy_warn_test.cc
    audit/rar_canonical_sign_test.cc
    audit/rar_capi_verify_test.cc
    audit/rar_require_signature_test.cc
  CATEGORIES audit failure
)

if(EBTREE_BUILD_SQL)
  ebtree_add_test_suite(
    NAME sql
    SOURCES
      ../test_sql/sql_smoke_test.cc
      ../test_sql/sql_open_attestation_test.cc
      ../test_sql/sql_mic_test.cc
      ../test_sql/sql_op_log_test.cc
      ../test_sql/sql_parse_matrix_test.cc
      ../test_sql/sql_exec_matrix_test.cc
      ../test_sql/sql_parse_advanced_test.cc
      ../test_sql/sql_join_subquery_test.cc
      ../test_sql/complex/sql_subquery_complex_test.cc
      ../test_sql/complex/sql_subquery_debt_test.cc
      ../test_sql/failure/sql_failure_test.cc
      ../test_sql/sql_data_matrix_test.cc
      ../test_sql/sql_row_codec_test.cc
      ../test_sql/sqllogic_runner_test.cc
      ../test_sql/sqllogic_baseline_test.cc
      ../test_sql/semantic_oracle_test.cc
      ../test_sql/exec/sql_perf_matrix_test.cc
      ../test_sql/sql_txn_test.cc
      ../test_sql/sql_advanced_exec_test.cc
      ../test_sql/c_api_smoke_test.cc
    CATEGORIES sql success
  )

  target_include_directories(ebtree_tests_sql PRIVATE
    "${CMAKE_SOURCE_DIR}/tools/ebtree_audit"
    "${CMAKE_SOURCE_DIR}"
    "${CMAKE_CURRENT_BINARY_DIR}"
    "${CMAKE_SOURCE_DIR}/test_sql"
  )
  target_link_libraries(ebtree_tests_sql PRIVATE ebtree_sql ebtree_sql_capi)

  set(SQL_MATRIX_DATA_DIR "${CMAKE_SOURCE_DIR}/test/data/sql")
  set(SQL_MATRIX_INC "${EBTEST_GENERATED_INC_DIR}/sql_matrix_inc.h")
  file(GLOB SQL_MATRIX_FILES "${SQL_MATRIX_DATA_DIR}/*.matrix")
  add_custom_command(
    OUTPUT "${SQL_MATRIX_INC}"
    COMMAND ${Python3_EXECUTABLE}
      "${CMAKE_SOURCE_DIR}/scripts/test/gen_sql_matrix_inc.py"
      --data-dir "${SQL_MATRIX_DATA_DIR}"
      --out "${SQL_MATRIX_INC}"
    DEPENDS
      "${CMAKE_SOURCE_DIR}/scripts/test/gen_sql_matrix_inc.py"
      ${SQL_MATRIX_FILES}
    VERBATIM
  )
  add_custom_target(ebtree_sql_matrix_inc DEPENDS "${SQL_MATRIX_INC}")
  add_dependencies(ebtree_tests_sql ebtree_sql_matrix_inc)
endif()

target_include_directories(ebtree_tests_audit PRIVATE
  "${CMAKE_SOURCE_DIR}/tools/ebtree_audit"
)
target_link_libraries(ebtree_tests_audit PRIVATE ebtree_audit_lib ebtree_audit_capi)
if(EBTREE_RAR_SIGNING)
  target_compile_definitions(ebtree_tests_audit PRIVATE
    EBTREE_RAR_SIGNING=1
    ED25519_NO_SEED
  )
  target_include_directories(ebtree_tests_audit PRIVATE
    "${CMAKE_SOURCE_DIR}/tools/ebtree_audit/third_party/ed25519"
  )
endif()

ebtree_generate_test_suites_inc("${CMAKE_CURRENT_BINARY_DIR}/generated/test_suites.inc")

add_executable(ebtree_test_runner runner/test_runner.cc)
target_include_directories(ebtree_test_runner PRIVATE
  "${CMAKE_CURRENT_BINARY_DIR}/generated"
  "${CMAKE_SOURCE_DIR}/gtest_capi/include"
)
if(MSVC)
  target_compile_definitions(ebtree_test_runner PRIVATE _CRT_SECURE_NO_WARNINGS)
endif()

foreach(_suite unit failure nested complex composite pipeline matrix audit)
  add_dependencies(ebtree_test_runner ebtree_tests_${_suite})
endforeach()
if(EBTREE_BUILD_SQL)
  add_dependencies(ebtree_test_runner ebtree_tests_sql)
endif()

set(_runner_out "${CMAKE_BINARY_DIR}/$<CONFIG>")
set_target_properties(ebtree_test_runner PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/$<CONFIG>"
  RUNTIME_OUTPUT_DIRECTORY_DEBUG "${CMAKE_BINARY_DIR}/Debug"
  RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}/Release"
)

add_test(NAME ebtree_all COMMAND ebtree_test_runner --suite=all)
set_tests_properties(ebtree_all PROPERTIES
  ENVIRONMENT "EBTREE_TEST_CONFIG=$<CONFIG>"
  WORKING_DIRECTORY "${CMAKE_BINARY_DIR}/$<CONFIG>"
)

add_test(NAME ebtree_pipeline COMMAND ebtree_test_runner --suite=pipeline)
set_tests_properties(ebtree_pipeline PROPERTIES
  ENVIRONMENT "EBTREE_TEST_CONFIG=$<CONFIG>"
  WORKING_DIRECTORY "${CMAKE_BINARY_DIR}/$<CONFIG>"
)

add_test(NAME ebtree_matrix COMMAND ebtree_test_runner --suite=matrix)
set_tests_properties(ebtree_matrix PROPERTIES
  ENVIRONMENT "EBTREE_TEST_CONFIG=$<CONFIG>"
  WORKING_DIRECTORY "${CMAKE_BINARY_DIR}/$<CONFIG>"
)
