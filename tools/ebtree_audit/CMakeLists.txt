cmake_minimum_required(VERSION 3.20)

add_library(ebtree_audit_lib STATIC
  digest.cc
  rar_types.cc
  physical_attestor.cc
  recovery_attestor.cc
  contract_attestor.cc
  policy_gate.cc
  json_writer.cc
  rar_builder.cc
  op_log_expect.cc
  catalog_expect.cc
  expect_loader.cc
  rar_sign.cc
  tier_consistency_attestor.cc
  rar_chain.cc
  rar_chain_anchor.cc
  rar_merkle.cc
  op_log_head_hash.cc
  rar_snapshot_builder.cc
  rar_chain_worker.cc
  rar_monitor.cc
  rar_chain_rotate.cc
)

if(EBTREE_RAR_SIGNING)
  target_sources(ebtree_audit_lib PRIVATE
    third_party/ed25519/seed.c
    third_party/ed25519/sign.c
    third_party/ed25519/verify.c
    third_party/ed25519/keypair.c
    third_party/ed25519/add_scalar.c
    third_party/ed25519/ge.c
    third_party/ed25519/sc.c
    third_party/ed25519/fe.c
    third_party/ed25519/sha512.c
  )
  target_compile_definitions(ebtree_audit_lib PRIVATE
    EBTREE_RAR_SIGNING=1
    ED25519_NO_SEED
  )
endif()

target_include_directories(ebtree_audit_lib
  PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}"
  PRIVATE
    "${CMAKE_SOURCE_DIR}/cpp/include"
    "${CMAKE_CURRENT_SOURCE_DIR}/third_party/ed25519"
)

target_link_libraries(ebtree_audit_lib PUBLIC ebtree_core)

if(MSVC)
  target_compile_options(ebtree_audit_lib PRIVATE /W4 /utf-8)
endif()

add_executable(ebtree_audit main.cc)
target_link_libraries(ebtree_audit PRIVATE ebtree_audit_lib)
target_include_directories(ebtree_audit PRIVATE "${CMAKE_SOURCE_DIR}/cpp/include")

add_library(ebtree_audit_capi STATIC
  "${CMAKE_SOURCE_DIR}/c_api/ebtree_audit.cc"
)
target_include_directories(ebtree_audit_capi
  PUBLIC "${CMAKE_SOURCE_DIR}/c_api"
  PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}"
)
target_link_libraries(ebtree_audit_capi PUBLIC ebtree_audit_lib)

set_target_properties(ebtree_audit PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/$<CONFIG>"
)
