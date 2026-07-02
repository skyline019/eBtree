cmake_minimum_required(VERSION 3.20)

add_library(ebtree_sql STATIC
  ast/query_ast.cc
  catalog/catalog.cc
  catalog/catalog_store.cc
  catalog/row_codec.cc
  eval/sql_value.cc
  eval/agg_engine.cc
  eval/expr_eval.cc
  eval/type_affinity.cc
  eval/like_match.cc
  eval/function_registry.cc
  exec/constraint_engine.cc
  exec/pragma_exec.cc
  exec/executor.cc
  exec/executor_v2.cc
  exec/executor_v3.cc
  exec/physical_scan.cc
  exec/dml_executor.cc
  exec/unified_executor.cc
  exec/subquery_runner.cc
  exec/mic_guard.cc
  op_log/op_log_writer.cc
  parse/bootstrap/parse_bootstrap.cc
  parse/core/lex_pipeline.cc
  parse/core/token_cursor.cc
  parse/core/stmt_classifier.cc
  parse/core/first_match_registry.cc
  parse/shared/parse_shared.cc
  parse/stmt/dml/install_dml_rules.cc
  parse/stmt/ddl/install_ddl_rules.cc
  parse/native/expr_parse.cc
  parse/native/select_parse.cc
  parse/native/advanced_parse.cc
  parse/native/stmt_handlers.cc
  parse/native/native_parser.cc
  parse/registry/registry_parser.cc
  plan/lower.cc
  plan/index_match.cc
  session/attestation.cc
  session/database.cc
  session/transaction_state.cc
  session/open_options.cc
)

target_include_directories(ebtree_sql
  PUBLIC
    "${CMAKE_SOURCE_DIR}"
  PRIVATE
    "${CMAKE_SOURCE_DIR}/cpp/include"
    "${CMAKE_SOURCE_DIR}/tools/ebtree_audit"
)

target_link_libraries(ebtree_sql
  PUBLIC
    ebtree_core
    ebtree_audit_lib
)

if(MSVC)
  target_compile_options(ebtree_sql PRIVATE /W4 /utf-8)
endif()

add_library(ebtree_sql_capi STATIC
  "${CMAKE_SOURCE_DIR}/c_api/ebtree_sql.cc"
)
target_include_directories(ebtree_sql_capi
  PUBLIC "${CMAKE_SOURCE_DIR}/c_api"
  PRIVATE
    "${CMAKE_SOURCE_DIR}"
    "${CMAKE_SOURCE_DIR}/cpp/include"
    "${CMAKE_SOURCE_DIR}/tools/ebtree_audit"
)
target_link_libraries(ebtree_sql_capi PUBLIC ebtree_sql)
