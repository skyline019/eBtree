#include "ebtree/common/digest.h"
#include "ebtree/engine/engine_attest.h"

#include "ebtree/engine/engine.h"
#include "ebtree/engine/shard_engine.h"
#include "ebtree/engine/shard_router.h"

#include <cstring>

namespace ebtree {

namespace {

AttestInferredPath InferPath(bool any_badwal, ShardRecoveryState state,
                             bool wal_replay_pending) {
  if (any_badwal) return AttestInferredPath::kTLogFallback;
  if (wal_replay_pending) return AttestInferredPath::kFastOpenDeferred;
  switch (state) {
    case ShardRecoveryState::kWalCorrupt:
      return AttestInferredPath::kTLogFallback;
    case ShardRecoveryState::kWalPending:
      return AttestInferredPath::kFastOpenDeferred;
    case ShardRecoveryState::kLazyKey:
      return AttestInferredPath::kLazyKey;
    case ShardRecoveryState::kOnDiskLazy:
      return AttestInferredPath::kOnDiskLazy;
    case ShardRecoveryState::kCommittedHot:
      return wal_replay_pending ? AttestInferredPath::kFastOpenDeferred
                                : AttestInferredPath::kWalReplayComplete;
    case ShardRecoveryState::kCommittedCold:
      return AttestInferredPath::kWalReplayComplete;
  }
  return AttestInferredPath::kUnknown;
}

ReadTier InferReadTierFromDelta(const uint64_t before[kReadTierCount],
                                const uint64_t after[kReadTierCount]) {
  for (size_t i = 0; i < kReadTierCount; ++i) {
    if (after[i] > before[i]) {
      return static_cast<ReadTier>(i);
    }
  }
  return ReadTier::kCount;
}

std::string Sha256HexLocal(const std::string& data) {
  return Sha256HexString(data);
}

AttestInferredPath WorstPath(AttestInferredPath a, AttestInferredPath b) {
  auto rank = [](AttestInferredPath p) {
    switch (p) {
      case AttestInferredPath::kTLogFallback: return 7;
      case AttestInferredPath::kFastOpenDeferred: return 6;
      case AttestInferredPath::kOnDiskLazy: return 5;
      case AttestInferredPath::kLazyKey: return 4;
      case AttestInferredPath::kWalReplayComplete: return 3;
      case AttestInferredPath::kCommittedHot: return 2;
      case AttestInferredPath::kUnknown: return 1;
    }
    return 0;
  };
  return rank(a) >= rank(b) ? a : b;
}

}  // namespace

std::string ReadTierToString(ReadTier tier) {
  switch (tier) {
    case ReadTier::kMemTable:
      return "MemTable";
    case ReadTier::kCommitted:
      return "Committed";
    case ReadTier::kBTreeDisk:
      return "BTreeDisk";
    case ReadTier::kDataFileLsn:
      return "DataFileLsn";
    case ReadTier::kWalSingleKey:
      return "WalSingleKey";
    case ReadTier::kCommittedDirectScan:
      return "CommittedDirectScan";
    case ReadTier::kBTreeScanResolve:
      return "BTreeScanResolve";
    case ReadTier::kTLogFlashback:
      return "TLogFlashback";
    case ReadTier::kCount:
      break;
  }
  return "Unknown";
}

std::string ShardRecoveryStateToString(ShardRecoveryState state) {
  switch (state) {
    case ShardRecoveryState::kWalCorrupt:
      return "WalCorrupt";
    case ShardRecoveryState::kOnDiskLazy:
      return "OnDiskLazy";
    case ShardRecoveryState::kWalPending:
      return "WalPending";
    case ShardRecoveryState::kLazyKey:
      return "LazyKey";
    case ShardRecoveryState::kCommittedCold:
      return "CommittedCold";
    case ShardRecoveryState::kCommittedHot:
      return "CommittedHot";
  }
  return "Unknown";
}

std::string AttestInferredPathToString(AttestInferredPath path) {
  switch (path) {
    case AttestInferredPath::kFastOpenDeferred:
      return "FastOpenDeferred";
    case AttestInferredPath::kWalReplayComplete:
      return "WalReplayComplete";
    case AttestInferredPath::kTLogFallback:
      return "TLogFallback";
    case AttestInferredPath::kLazyKey:
      return "LazyKey";
    case AttestInferredPath::kOnDiskLazy:
      return "OnDiskLazy";
    case AttestInferredPath::kCommittedHot:
      return "CommittedHot";
    case AttestInferredPath::kUnknown:
      break;
  }
  return "Unknown";
}

std::string RecoveryModeToString(RecoveryMode mode) {
  switch (mode) {
    case RecoveryMode::kHot:
      return "Hot";
    case RecoveryMode::kLazy:
      return "Lazy";
    case RecoveryMode::kFull:
      return "Full";
  }
  return "Hot";
}

Status AttestExport(Engine* engine, const AttestExportOptions& opts,
                    AttestExportReport* out) {
  if (!engine || !out) return Status::InvalidArgument("null argument");

  out->recovery.recovery_mode = engine->recovery_mode();
  out->recovery.wal_replay_pending = engine->wal_replay_pending();
  out->recovery.unexpected_path_total = engine->stats().unexpected_path_total;
  out->recovery.stable_lsn = engine->stats().stable_lsn;
  out->recovery.shards.clear();

  const uint32_t n = engine->shard_count();
  AttestInferredPath aggregate = AttestInferredPath::kUnknown;
  for (uint32_t i = 0; i < n; ++i) {
    RecoveryShardSnapshot snap{};
    snap.shard_id = i;
    const ShardEngine* shard = engine->shard(i);
    if (!shard) {
      snap.state = ShardRecoveryState::kCommittedHot;
      snap.inferred_path =
          InferPath(opts.any_badwal, snap.state, out->recovery.wal_replay_pending);
      aggregate = WorstPath(aggregate, snap.inferred_path);
      out->recovery.shards.push_back(snap);
      continue;
    }
    snap.state = shard->recovery_state();
    snap.wal_corrupt = shard->wal_corrupt();
    snap.lazy_root_corrupt = shard->lazy_root_corrupt();
    snap.inferred_path =
        InferPath(opts.any_badwal || snap.wal_corrupt, snap.state,
                  out->recovery.wal_replay_pending);
    aggregate = WorstPath(aggregate, snap.inferred_path);
    for (size_t t = 0; t < kReadTierCount; ++t) {
      snap.read_tier_hits[t] = shard->stats().read_tier_hits[t];
    }
    out->recovery.shards.push_back(snap);
  }
  out->inferred_path = aggregate;

  out->probes.clear();
  out->probes.reserve(opts.probe_keys.size());
  for (const auto& key : opts.probe_keys) {
    AttestKeyProbe probe{};
    probe.key = key;
    probe.shard = RouteShard(key, engine->shard_count());

    uint64_t before[kReadTierCount]{};
    uint64_t after[kReadTierCount]{};
    if (const ShardEngine* shard = engine->shard(probe.shard)) {
      for (size_t i = 0; i < kReadTierCount; ++i) {
        before[i] = shard->stats().read_tier_hits[i];
      }
    }

    std::string value;
    const Status st = engine->Get(key, &value);
    probe.found = st.ok();
    if (probe.found) {
      probe.value_sha256 = Sha256HexLocal(value);
    }

    if (const ShardEngine* shard = engine->shard(probe.shard)) {
      for (size_t i = 0; i < kReadTierCount; ++i) {
        after[i] = shard->stats().read_tier_hits[i];
      }
    }
    probe.read_tier = InferReadTierFromDelta(before, after);
    out->probes.push_back(std::move(probe));
  }

  return Status::Ok();
}

}  // namespace ebtree
