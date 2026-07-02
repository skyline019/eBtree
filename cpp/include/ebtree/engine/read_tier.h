#pragma once

#include <cstdint>

namespace ebtree {

enum class ReadTier : uint8_t {
  kMemTable = 0,
  kCommitted,
  kBTreeDisk,
  kDataFileLsn,
  kVersionChain,
  kWalSingleKey,
  kWalSnapshotKey,
  kCommittedDirectScan,
  kBTreeScanResolve,
  kTLogFlashback,
  kCount,
};

constexpr size_t kReadTierCount = static_cast<size_t>(ReadTier::kCount);

void RecordReadTier(ReadTier tier, uint64_t* tier_hits, uint64_t* unexpected_path);

}  // namespace ebtree
