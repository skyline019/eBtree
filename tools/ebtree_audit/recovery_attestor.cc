#include "recovery_attestor.h"

#include "ebtree/engine/engine_attest.h"

namespace ebtree {
namespace audit {

namespace {

InferredRecoveryPath MapInferredPath(AttestInferredPath path) {
  switch (path) {
    case AttestInferredPath::kFastOpenDeferred:
      return InferredRecoveryPath::kFastOpenDeferred;
    case AttestInferredPath::kWalReplayComplete:
      return InferredRecoveryPath::kWalReplayComplete;
    case AttestInferredPath::kTLogFallback:
      return InferredRecoveryPath::kTLogFallback;
    case AttestInferredPath::kLazyKey:
      return InferredRecoveryPath::kLazyKey;
    case AttestInferredPath::kOnDiskLazy:
      return InferredRecoveryPath::kOnDiskLazy;
    case AttestInferredPath::kCommittedHot:
      return InferredRecoveryPath::kCommittedHot;
    case AttestInferredPath::kUnknown:
      break;
  }
  return InferredRecoveryPath::kUnknown;
}

}  // namespace

Status RecoveryAttest(Engine* engine, const std::vector<std::string>& keys,
                      bool any_badwal, RecoveryReport* out) {
  if (!engine || !out) return Status::InvalidArgument("null argument");

  AttestExportOptions opts{};
  opts.any_badwal = any_badwal;
  opts.probe_keys = keys;

  AttestExportReport report{};
  const Status st = AttestExport(engine, opts, &report);
  if (!st.ok()) return st;

  out->recovery_mode = RecoveryModeToString(report.recovery.recovery_mode);
  out->wal_replay_pending = report.recovery.wal_replay_pending;
  out->unexpected_path_total = report.recovery.unexpected_path_total;
  out->stable_lsn = report.recovery.stable_lsn;
  out->inferred_path = MapInferredPath(report.inferred_path);
  if (out->inferred_path == InferredRecoveryPath::kUnknown &&
      !report.recovery.shards.empty()) {
    out->inferred_path = MapInferredPath(report.recovery.shards[0].inferred_path);
  }

  out->shard_state.clear();
  out->shard_state.reserve(report.recovery.shards.size());
  for (const auto& shard : report.recovery.shards) {
    RecoveryShardReport sr{};
    sr.shard_id = shard.shard_id;
    sr.state = ebtree::ShardRecoveryStateToString(shard.state);
    sr.inferred_path = MapInferredPath(shard.inferred_path);
    for (size_t t = 0; t < kReadTierCount; ++t) {
      if (shard.read_tier_hits[t] > 0) {
        sr.read_tier_hits[ebtree::ReadTierToString(static_cast<ReadTier>(t))] =
            shard.read_tier_hits[t];
      }
    }
    out->shard_state.push_back(std::move(sr));
  }

  out->probes.clear();
  out->probes.reserve(report.probes.size());
  for (const auto& probe : report.probes) {
    KeyProbeResult kp{};
    kp.shard = probe.shard;
    kp.key = probe.key;
    kp.found = probe.found;
    kp.value_sha256 = probe.value_sha256;
    if (probe.read_tier != ReadTier::kCount) {
      kp.read_tier = ebtree::ReadTierToString(probe.read_tier);
    } else {
      kp.read_tier = "Unknown";
    }
    out->probes.push_back(std::move(kp));
  }

  return Status::Ok();
}

}  // namespace audit
}  // namespace ebtree
