#pragma once

#include <cstdint>
#include <map>
#include <memory>
#include <atomic>
#include <mutex>
#include <shared_mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include "ebtree/common/config.h"
#include "ebtree/common/status.h"
#include "ebtree/concept/btree/btree.h"
#include "ebtree/concept/datafile/datafile.h"
#include "ebtree/concept/gc/region_manager.h"
#include "ebtree/concept/group_commit/group_committer.h"
#include "ebtree/concept/memtable/memtable.h"
#include "ebtree/concept/mmap/mmap_window.h"
#include "ebtree/concept/recovery/lazy_recovery.h"
#include "ebtree/concept/recovery/recovery_state.h"
#include "ebtree/concept/superblock/superblock.h"
#include "ebtree/concept/tlog/tlog.h"
#include "ebtree/concept/wal/wal.h"
#include "ebtree/concept/wal/wal_fsync_coordinator.h"
#include "ebtree/concept/wal/wal_batch_pipeline.h"
#include "ebtree/concept/datafile/datafile_reader.h"
#include "ebtree/concept/vcs/version_chain_store.h"
#include "ebtree/concept/wal/txidx.h"
#include "ebtree/engine/flush_worker.h"
#include "ebtree/engine/read_tier.h"
#include "ebtree/engine/sfs_read_cache.h"
#include "ebtree/engine/snapshot.h"
#include "ebtree/engine/snapshot_fair_rw_lock.h"
#include "ebtree/sync/sync_executor.h"

namespace ebtree {

class ReadResolver;
class ScanResolver;
class SnapshotResolver;
class BackgroundSummaryValidator;

class ShardEngine {
 public:
  static Status Create(uint32_t shard_id, const EngineOptions& opts,
                       std::unique_ptr<ShardEngine>* out);

  Status Put(const std::string& key, const std::string& value,
             uint32_t txn_id = 0);
  Status Delete(const std::string& key, uint32_t txn_id = 0);
  Status Get(const std::string& key, std::string* value);
  Status GetAtSnapshot(const std::string& key, const SnapshotReadContext& ctx,
                       std::string* value);
  Status ResolveLsnAtSnapshot(const std::string& key,
                              const SnapshotReadContext& ctx, uint64_t* lsn_out);
  Status ResolveCurrentCommittedLsn(const std::string& key, uint64_t* lsn_out);
  Status RefreshExternalWalIfPending();
  Status ScanAtSnapshot(const TypedPlan& plan, const SnapshotReadContext& ctx,
                        std::vector<std::pair<std::string, std::string>>* rows);
  Status GetAsOfLsn(const std::string& key, uint64_t lsn, std::string* value);
  Status Scan(const TypedPlan& plan,
              std::vector<std::pair<std::string, std::string>>* rows);
  Status GetAsOf(const std::string& key, uint32_t timestamp_sec,
                 std::string* value);
  Status ScanAsOf(const TypedPlan& plan, uint32_t timestamp_sec,
                  std::vector<std::pair<std::string, std::string>>* rows);

  Status Prepare(const TypedPlan& plan) const;
  Status Flush();
  Status Checkpoint();
  Status Recover();
  Status RecoverFull();
  Status GroupCommit();

  Status RecoverFromSuperBlock(const SuperBlock& sb);

  Status CorruptSuperBlockForTest();
  Status CorruptSuperBlockSlotForTest(int slot);
  Status CorruptRootForTest();
  Status CorruptWalForTest();
  Status CorruptDataFileForTest(uint64_t record_offset);
  Status TruncateWalForTest();
  void ClearVcsForTest();

  void SetCheckpointHookForTest(CheckpointHook hook);

  RecoveryMode recovery_mode() const { return recovery_mode_; }
  ShardRecoveryState recovery_state() const { return recovery_state_; }
  bool wal_replay_pending() const { return wal_replay_pending_; }
  bool wal_corrupt() const { return wal_corrupt_; }
  bool lazy_root_corrupt() const { return lazy_root_corrupt_; }
  uint32_t shard_id() const { return shard_id_; }

  const EngineStats& stats() const { return stats_; }
  EngineStats* mutable_stats() { return &stats_; }
  uint64_t stable_lsn() const { return stats_.stable_lsn; }
  VersionChainStore* vcs() { return vcs_.get(); }
  const VersionChainStore* vcs() const { return vcs_.get(); }
  void PinSnapshot(uint64_t lsn);
  void ReleaseSnapshot(uint64_t lsn);
  uint64_t pinned_epoch_lsn() const { return pinned_epoch_lsn_; }
  Status AppendTxnBegin(uint32_t txn_id, uint64_t snapshot_lsn);
  Status AppendTxnCommit(uint32_t txn_id);
  Status AppendTxnAbort(uint32_t txn_id);
  Status SaveTxnSidecar() const;
  Status LoadTxnSidecar();
  uint32_t snapshot_pin_count() const {
    return snapshot_pin_count_.load(std::memory_order_acquire);
  }
  uint64_t pinned_snapshot_lsn() const { return pinned_snapshot_lsn_; }
  SyncExecutor* sync() { return &sync_; }
  SyncContext* sync_context() { return &sync_ctx_; }

  MemTable* memtable() { return &memtable_; }
  MemTable* immutable_memtable() { return &immutable_; }
  MemTable* flushing_memtable() { return &flushing_; }
  MemTable* frozen_memtable() { return &flushing_; }
  WalWriter* wal() { return wal_.get(); }
  DataFile* datafile() { return datafile_.get(); }
  SuperBlockStore* superblock() { return superblock_.get(); }
  TLogWriter* tlog() { return tlog_.get(); }
  RegionManager* gc() { return gc_.get(); }
  BTreeIndex* btree() { return &btree_; }
  MmapWindowManager* mmap_manager() { return &mmap_mgr_; }

  const SuperBlock& loaded_superblock() const { return sb_; }

  const std::unordered_map<std::string, std::pair<std::string, uint64_t>>&
  committed() const {
    return committed_;
  }

  DurabilityClass durability() const { return opts_.durability; }
  const EngineOptions& options() const { return opts_; }

  Status FlushInternal();
  Status TryFlushBackground();
  Status CommitSuperBlockInternal();
  Status AppendTLogSnapshot();
  Status FoldWalToVcs();
  Status SaveVcsSidecar() const;
  Status LoadVcsSidecar();
  Status RepairSummary();
  bool TryRepairSummaryIfDrifted();
  bool TryRepairSummaryIfDriftedBackground();
  void RotateMemTableForFlush();

  GroupCommitState* group_commit_state() { return &group_commit_; }

  std::unordered_map<std::string, std::pair<std::string, uint64_t>>&
  committed_mut() {
    return committed_;
  }

  ~ShardEngine();

  friend class ReadResolver;
  friend class ScanResolver;
  friend class SnapshotResolver;

  std::unique_ptr<VersionChainStore> vcs_;

 private:
  ShardEngine(uint32_t shard_id, EngineOptions opts);

  Status OpenInternal();
  Status ReadVisible(const std::string& key, std::string* value,
                     uint64_t snapshot_lsn);
  Status ReadVisible(const std::string& key, std::string* value,
                     const SnapshotReadContext& ctx);
  Status EnsureWalReplayed();
  bool SnapshotReadNeedsUpgrade(uint64_t snapshot_lsn) const;
  Status PrepareSnapshotReadLocked(uint64_t snapshot_lsn);
  Status GetAtSnapshotCore(const std::string& key, const SnapshotReadContext& ctx,
                           std::string* value);
  Status RestoreKeyFromWal(const std::string& key);
  Status MaybeGcSwap();
  Status BTreeScanWithHeal(const TypedPlan& plan,
                           std::vector<std::pair<std::string, uint64_t>>* hits);
  void OverlayMemTableHits(
      const TypedPlan& plan,
      std::vector<std::pair<std::string, uint64_t>>* hits) const;
  void OverlayCommittedHits(
      const TypedPlan& plan, uint64_t snapshot_lsn,
      std::vector<std::pair<std::string, uint64_t>>* hits) const;
  void OverlaySnapshotMemTableHits(
      const TypedPlan& plan, const SnapshotReadContext& ctx,
      std::vector<std::pair<std::string, uint64_t>>* hits) const;
  Status LoadDatafileOnRecover(const SuperBlock& sb,
                               std::unordered_map<std::string,
                                                  std::pair<std::string, uint64_t>>*
                                   out,
                               uint64_t* data_max);
  Status LoadAsOfCommitted(uint32_t timestamp_sec,
                           std::unordered_map<std::string,
                                              std::pair<std::string, uint64_t>>*
                               out,
                           uint64_t* max_lsn);
  Status ReadValueByLsn(uint64_t lsn, std::string* value);
  Status ResolveScanValues(
      const std::vector<std::pair<std::string, uint64_t>>& hits,
      uint64_t snapshot_lsn,
      std::vector<std::pair<std::string, std::string>>* rows);
  Status ResolveScanValues(
      const std::vector<std::pair<std::string, uint64_t>>& hits,
      const SnapshotReadContext& ctx,
      std::vector<std::pair<std::string, std::string>>* rows);
  bool MemTablesEmpty() const;
  bool CanScanCommittedDirect(const TypedPlan& plan) const;
  Status ScanCommittedDirect(const TypedPlan& plan, uint64_t snapshot_lsn,
                             std::vector<std::pair<std::string, std::string>>* rows) const;
  Status PutSyncFastAppend(const std::string& key, const std::string& value,
                           uint64_t* lsn, uint32_t txn_id = 0);
  Status DeleteSyncFastAppend(const std::string& key, uint64_t* lsn,
                              uint32_t txn_id = 0);
  Status PutSyncFast(const std::string& key, const std::string& value);
  Status DeleteSyncFast(const std::string& key);
  Status ApplyWalBatchLocked(const std::vector<WalBatchCommitItem>& items,
                             EngineStats* stats);
  Status ApplyWalBatchUnlocked(const std::vector<WalBatchCommitItem>& items,
                               EngineStats* stats);
  void RefreshRecoveryState();
  void RecordTier(ReadTier tier) const;

  uint32_t shard_id_{0};
  EngineOptions opts_;
  mutable EngineStats stats_{};
  SyncExecutor sync_;
  SyncContext sync_ctx_{};

  MemTable memtable_;
  MemTable immutable_;
  MemTable flushing_;
  std::unique_ptr<WalWriter> wal_;
  std::unique_ptr<DataFile> datafile_;
  std::unique_ptr<SuperBlockStore> superblock_;
  std::unique_ptr<TLogWriter> tlog_;
  std::unique_ptr<RegionManager> gc_;
  BTreeIndex btree_;
  MmapWindowManager mmap_mgr_;

  std::unordered_map<std::string, std::pair<std::string, uint64_t>> committed_;
  SuperBlock sb_{};
  GroupCommitState group_commit_{};
  LazyRecoveryState lazy_state_{};
  RecoveryMode recovery_mode_{RecoveryMode::kHot};
  ShardRecoveryState recovery_state_{ShardRecoveryState::kCommittedHot};
  bool wal_replay_pending_{false};
  bool lazy_root_corrupt_{false};
  bool wal_corrupt_{false};
  mutable SnapshotFairRwLock rw_mu_;
  std::atomic<uint32_t> snapshot_pin_count_{0};
  uint64_t pinned_snapshot_lsn_{0};
  uint64_t pinned_epoch_lsn_{0};
  SfsReadCache sfs_cache_;
  TxnSidecarStore txn_sidecar_;
  std::unique_ptr<BackgroundFlushWorker> flush_worker_;
  std::unique_ptr<BackgroundSummaryValidator> summary_validator_;
  std::unique_ptr<WalFsyncCoordinator> fsync_coordinator_;
  std::unique_ptr<WalBatchPipeline> wal_batch_pipeline_;
  CheckpointHook checkpoint_hook_;
  DataFileReader datafile_reader_{nullptr, nullptr};
  bool opened_{false};
};

}  // namespace ebtree
