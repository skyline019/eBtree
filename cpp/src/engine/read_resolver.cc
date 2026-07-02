#include "ebtree/engine/read_resolver.h"

#include "ebtree/concept/btree/btree.h"
#include "ebtree/concept/wal/wal.h"
#include "ebtree/engine/read_tier.h"
#include "ebtree/engine/shard_engine.h"
#include "ebtree/engine/tier_dispatch.h"
#include "ebtree/engine/write_request.h"

namespace ebtree {

bool ReadResolver::TryLazyWalOrDisk(ShardEngine& shard, const std::string& key,
                                    const TypedPlan& plan, std::string* value) {
  uint64_t data_lsn = 0;
  const bool btree_hit = shard.btree_.Get(key, &data_lsn).ok();
  if (btree_hit && shard.wal_replay_pending_ && !shard.wal_corrupt_) {
    uint64_t wal_off = 0;
    const uint64_t wal_cutoff = shard.loaded_superblock().critical.wal_lsn;
    if (shard.wal()->key_index().Lookup(key, wal_cutoff, &wal_off)) {
      WalOp op = WalOp::kPut;
      std::string wal_key = key;
      std::string wal_val;
      uint64_t wal_rec_lsn = 0;
      if (shard.wal()->ReplayRecordAt(wal_off, &op, &wal_key, &wal_val, &wal_rec_lsn)
              .ok() &&
          wal_rec_lsn > data_lsn) {
        shard.RecordTier(ReadTier::kWalSingleKey);
        const Status restore = shard.RestoreKeyFromWal(key);
        if (restore.ok()) {
          return shard.ReadVisible(key, value, plan.snapshot_lsn).ok();
        }
      }
    }
  }
  if (btree_hit) {
    if (plan.snapshot_lsn == 0 || data_lsn <= plan.snapshot_lsn) {
      shard.RecordTier(ReadTier::kBTreeDisk);
      const Status disk = shard.ReadValueByLsn(data_lsn, value);
      if (disk.ok()) return true;
    }
  }
  if (shard.recovery_mode_ == RecoveryMode::kLazy || shard.lazy_root_corrupt_) {
    shard.RecordTier(ReadTier::kWalSingleKey);
    const Status restore = shard.RestoreKeyFromWal(key);
    if (restore.ok()) {
      return shard.ReadVisible(key, value, plan.snapshot_lsn).ok();
    }
  }
  return false;
}

Status ReadResolver::Get(ShardEngine& shard, const std::string& key,
                         std::string* value) {
  if (!value) return Status::InvalidArgument("value is null");
  TypedPlan plan;
  plan.op = PredicateOp::kEq;
  plan.key = key;
  {
    std::shared_lock<SnapshotFairRwLock> lock(shard.rw_mu_);
    plan.snapshot_lsn = shard.stats_.stable_lsn;
    const Status pst = shard.Prepare(plan);
    if (!pst.ok() && pst.code() != StatusCode::kStaleSummary) return pst;
    g_write_req.key = key;
    Status rs = shard.ReadVisible(key, value, plan.snapshot_lsn);
    const bool stale_summary =
        plan.snapshot_lsn > 0 &&
        shard.btree_.summary().summary_lsn < plan.snapshot_lsn;
    if (rs.ok() && !stale_summary) return rs;
    if (rs.code() == StatusCode::kNotFound &&
        (shard.opts_.lazy_committed_load || shard.btree_.on_disk_mode())) {
      if (TryLazyWalOrDisk(shard, key, plan, value)) return Status::Ok();
    }
    if (shard.recovery_mode_ == RecoveryMode::kLazy &&
        rs.code() == StatusCode::kNotFound) {
      shard.RecordTier(ReadTier::kWalSingleKey);
      const Status restore = shard.RestoreKeyFromWal(key);
      if (restore.ok()) {
        return shard.ReadVisible(key, value, plan.snapshot_lsn);
      }
    }
    if (!stale_summary && (!shard.wal_replay_pending_ || shard.wal_corrupt_)) {
      return rs;
    }
  }

  std::unique_lock<SnapshotFairRwLock> lock(shard.rw_mu_);
  plan.snapshot_lsn = shard.stats_.stable_lsn;
  if (plan.snapshot_lsn > 0 &&
      shard.btree_.summary().summary_lsn < plan.snapshot_lsn) {
    const Status repair = shard.RepairSummary();
    if (!repair.ok()) return repair;
  }
  if (shard.wal_replay_pending_ && !shard.wal_corrupt_ &&
      shard.recovery_mode_ != RecoveryMode::kLazy) {
    const Status replay = shard.EnsureWalReplayed();
    if (!replay.ok()) return replay;
    shard.RefreshRecoveryState();
  }
  g_write_req.key = key;
  Status rs = shard.ReadVisible(key, value, plan.snapshot_lsn);
  if (rs.ok()) return rs;
  if (shard.opts_.lazy_committed_load || shard.btree_.on_disk_mode()) {
    if (TryLazyWalOrDisk(shard, key, plan, value)) return Status::Ok();
  }
  return rs;
}

}  // namespace ebtree
