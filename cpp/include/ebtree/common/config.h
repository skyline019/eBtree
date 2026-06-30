#pragma once

#include <cstdint>
#include <string>

#include "ebtree/engine/read_tier.h"

#include <functional>

namespace ebtree {

enum class CheckpointPhase {
  AfterFlush,
  AfterTLog,
  BeforeSuperBlock,
  AfterSuperBlock,
};

using CheckpointHook = std::function<bool(CheckpointPhase)>;

enum class DurabilityClass {
  kSync,
  kBalanced,
  kGroup,
  kAsync,
};

enum class RecoveryMode { kHot, kLazy, kFull };

enum class RecoveryStrategy { kFastOpen, kFullReplay };

struct EngineOptions {
  std::string path;
  DurabilityClass durability{DurabilityClass::kGroup};
  bool sync_on_commit{false};
  uint32_t shard_count{1};
  uint32_t group_commit_batch_size{64};
  uint32_t fsync_batch_size{1};
  uint32_t fsync_max_wait_us{0};
  uint32_t wal_durable_batch_bytes{4096};
  RecoveryStrategy recovery_strategy{RecoveryStrategy::kFastOpen};
  uint64_t gc_reclaim_threshold_bytes{0};
  bool background_flush{false};
  bool background_summary_validate{true};
  uint32_t memtable_flush_threshold_keys{4096};
  size_t page_cache_capacity{64};
  bool prefer_histogram_summary{false};
  bool lazy_committed_load{false};
  bool eager_shard_open{false};
  bool compress_values{false};
  bool compress_pages{false};

  static EngineOptions ProductionDefaults(const std::string& path);
  static EngineOptions StandardDefaults(const std::string& path);
  static EngineOptions EnterpriseDefaults(const std::string& path);
  static EngineOptions BenchmarkGroupDefaults(const std::string& path);
};

struct EngineStats {
  uint64_t fallback_read_total{0};
  uint64_t bypass_prepare_total{0};
  uint64_t wal_append_total{0};
  uint64_t superblock_commit_total{0};
  uint64_t stable_lsn{0};
  uint64_t group_commit_total{0};
  uint64_t flusher_flush_total{0};
  uint64_t recovery_total{0};
  uint64_t lazy_page_faults{0};
  uint64_t summary_repair_total{0};
  uint64_t tlog_snapshot_total{0};
  uint64_t gc_region_swap_total{0};
  uint64_t wal_replay_deferred_total{0};
  uint64_t pages_touched{0};
  uint64_t wal_full_scan_total{0};
  uint64_t unexpected_path_total{0};
  uint64_t read_tier_hits[kReadTierCount]{};
  uint64_t fsync_batch_total{0};
  uint64_t fsync_waiter_total{0};
  uint64_t fsync_merge_ratio{0};
  uint64_t compress_bytes_in{0};
  uint64_t compress_bytes_out{0};
};

}  // namespace ebtree
