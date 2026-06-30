#include "ebtree/engine/scan_resolver.h"

#include "ebtree/concept/recovery/recovery_state.h"
#include "ebtree/concept/page/page_format.h"
#include "ebtree/engine/read_tier.h"
#include "ebtree/engine/shard_engine.h"

namespace ebtree {

Status ScanResolver::Scan(ShardEngine& shard, const TypedPlan& plan,
                          std::vector<std::pair<std::string, std::string>>* rows) {
  if (!rows) return Status::InvalidArgument("rows is null");
  if (shard.wal_replay_pending_ && !shard.wal_corrupt_ &&
      shard.recovery_mode_ != RecoveryMode::kLazy) {
    std::unique_lock<std::shared_mutex> lock(shard.rw_mu_);
    const Status replay = shard.EnsureWalReplayed();
    if (!replay.ok()) return replay;
    shard.RefreshRecoveryState();
  }
  std::shared_lock<std::shared_mutex> lock(shard.rw_mu_);
  const uint64_t snapshot_lsn =
      plan.snapshot_lsn > 0 ? plan.snapshot_lsn : shard.stats_.stable_lsn;
  const Status pst = shard.Prepare(plan);
  if (!pst.ok() && pst.code() != StatusCode::kStaleSummary) return pst;

  if (shard.recovery_state_ == ShardRecoveryState::kCommittedCold &&
      shard.CanScanCommittedDirect(plan)) {
    shard.RecordTier(ReadTier::kCommittedDirectScan);
    const Status direct =
        shard.ScanCommittedDirect(plan, snapshot_lsn, rows);
    lock.unlock();
    return direct;
  }

  std::vector<std::pair<std::string, uint64_t>> hits;
  Status st = shard.btree_.Scan(plan, &hits);
  if (st == StatusCode::kStaleSummary) {
    lock.unlock();
    std::unique_lock<std::shared_mutex> unique(shard.rw_mu_);
    const Status repair = shard.RepairSummary();
    if (!repair.ok()) return repair;
    hits.clear();
    st = shard.btree_.Scan(plan, &hits);
    if (st == StatusCode::kStaleSummary) {
      hits.clear();
      st = Status::Ok();
    }
    if (!st.ok()) return st;
    shard.OverlayMemTableHits(plan, &hits);
    if (shard.lazy_root_corrupt_ ||
        shard.sb_.critical.active_root < kPageFileHeaderSize ||
        !shard.btree_.on_disk_mode()) {
      shard.OverlayCommittedHits(plan, snapshot_lsn, &hits);
    }
    rows->clear();
    shard.RecordTier(ReadTier::kBTreeScanResolve);
    unique.unlock();
    return shard.ResolveScanValues(hits, snapshot_lsn, rows);
  }
  if (!st.ok()) return st;
  shard.OverlayMemTableHits(plan, &hits);
  shard.stats_.pages_touched = shard.btree_.pages_touched();
  if (shard.lazy_root_corrupt_ ||
      shard.sb_.critical.active_root < kPageFileHeaderSize ||
      !shard.btree_.on_disk_mode()) {
    shard.OverlayCommittedHits(plan, snapshot_lsn, &hits);
  }
  shard.RecordTier(ReadTier::kBTreeScanResolve);
  lock.unlock();
  return shard.ResolveScanValues(hits, snapshot_lsn, rows);
}

}  // namespace ebtree
