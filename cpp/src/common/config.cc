#include "ebtree/common/config.h"

namespace ebtree {

namespace {

EngineOptions BalancedProductOptions(const std::string& path) {
  EngineOptions opts;
  opts.path = path;
  opts.durability = DurabilityClass::kBalanced;
  opts.sync_on_commit = false;
  opts.background_flush = true;
  opts.page_cache_capacity = 128;
  opts.prefer_histogram_summary = true;
  opts.lazy_committed_load = true;
  opts.fsync_batch_size = 512;
  opts.fsync_max_wait_us = 600;
  opts.wal_durable_batch_bytes = 16384;
  opts.gc_reclaim_threshold_bytes = 32u * 1024u * 1024u;
  return opts;
}

}  // namespace

EngineOptions EngineOptions::ProductionDefaults(const std::string& path) {
  return BalancedProductOptions(path);
}

EngineOptions EngineOptions::StandardDefaults(const std::string& path) {
  return ProductionCompressDefaults(path);
}

EngineOptions EngineOptions::EnterpriseDefaults(const std::string& path) {
  EngineOptions opts;
  opts.path = path;
  opts.durability = DurabilityClass::kSync;
  opts.sync_on_commit = true;
  opts.background_flush = true;
  opts.page_cache_capacity = 128;
  opts.prefer_histogram_summary = true;
  opts.lazy_committed_load = true;
  opts.fsync_batch_size = 1;
  opts.fsync_max_wait_us = 0;
  return opts;
}

EngineOptions EngineOptions::BenchmarkGroupDefaults(const std::string& path) {
  EngineOptions opts;
  opts.path = path;
  opts.durability = DurabilityClass::kGroup;
  opts.sync_on_commit = false;
  opts.group_commit_batch_size = 512;
  opts.page_cache_capacity = 256;
  opts.prefer_histogram_summary = true;
  return opts;
}

EngineOptions EngineOptions::ProductionCompressDefaults(const std::string& path) {
  EngineOptions opts = ProductionDefaults(path);
  opts.path = path;
  opts.compress_values = true;
  opts.compress_policy = CompressPolicy::kFastOnly;
  return opts;
}

EngineOptions EngineOptions::EnterpriseCompressDefaults(const std::string& path) {
  EngineOptions opts = EnterpriseDefaults(path);
  opts.path = path;
  opts.compress_values = true;
  opts.compress_policy = CompressPolicy::kBalanced;
  opts.compress_pages = true;
  opts.gc_reclaim_threshold_bytes = 64u * 1024u * 1024u;
  return opts;
}

}  // namespace ebtree
