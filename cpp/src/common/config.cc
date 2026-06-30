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
  return opts;
}

}  // namespace

EngineOptions EngineOptions::ProductionDefaults(const std::string& path) {
  return BalancedProductOptions(path);
}

EngineOptions EngineOptions::StandardDefaults(const std::string& path) {
  return ProductionDefaults(path);
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

}  // namespace ebtree
