cmake_minimum_required(VERSION 3.20)

include("${CMAKE_SOURCE_DIR}/cmake/7zip_lzma.cmake")

set(EBTREE_CORE_SOURCES
  src/common/status.cc
  src/common/config.cc
  src/common/digest.cc
  src/concept/superblock/superblock.cc
  src/concept/wal/wal.cc
  src/concept/wal/wal_key_index.cc
  src/concept/memtable/memtable.cc
  src/concept/datafile/datafile.cc
  src/concept/datafile/datafile_lsn_index.cc
  src/concept/codec/value_codec.cc
  src/concept/codec/lzma_codec.cc
  src/concept/mmap/mmap_window.cc
  src/concept/flusher/flusher.cc
  src/concept/group_commit/group_committer.cc
  src/concept/btree/btree.cc
  src/concept/btree/paged_btree.cc
  src/concept/btree/summary_healer.cc
  src/concept/page/page_file.cc
  src/concept/tlog/tlog.cc
  src/concept/gc/region_manager.cc
  src/concept/recovery/recovery_state.cc
  src/concept/datafile/datafile_reader.cc
  src/concept/wal/wal_segment.cc
  src/concept/wal/wal_fsync_coordinator.cc
  src/concept/wal/wal_batch_pipeline.cc
  src/engine/read_tier.cc
  src/engine/read_resolver.cc
  src/engine/scan_resolver.cc
  src/sync/sync_executor.cc
  src/sync/rules/core_rules.cc
  src/sync/rules/durability_rules.cc
  src/sync/rules/recovery_rules.cc
  src/engine/engine.cc
  src/engine/engine_attest.cc
  src/engine/shard_engine.cc
  src/engine/shard_router.cc
  src/engine/routing_table.cc
  src/engine/flush_worker.cc
  src/engine/summary_validator.cc
  src/engine/write_request.cc
)

add_library(ebtree_core STATIC ${EBTREE_CORE_SOURCES})

target_include_directories(ebtree_core
  PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
)

target_link_libraries(ebtree_core PUBLIC ebtree_lzma)

if(MSVC)
  target_compile_options(ebtree_core PRIVATE /W4 /utf-8)
  target_compile_definitions(ebtree_core PRIVATE _SILENCE_CXX17_CODECVT_HEADER_DEPRECATION_WARNING)
  target_compile_definitions(ebtree_core PRIVATE $<$<CONFIG:Release>:EBTREE_SIMD=1>)
  target_compile_options(ebtree_core PRIVATE
    $<$<CONFIG:Release>:/O2 /Ob2 /Oi /GL /arch:AVX2 /fp:fast>)
  target_link_libraries(ebtree_core PRIVATE winmm)
endif()
