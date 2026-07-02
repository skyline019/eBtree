#pragma once

#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include "ebtree/common/config.h"
#include "ebtree/common/status.h"
#include "ebtree/concept/btree/btree.h"
#include "ebtree/concept/datafile/datafile.h"
#include "ebtree/concept/gc/region_manager.h"
#include "ebtree/concept/memtable/memtable.h"
#include "ebtree/concept/superblock/superblock.h"
#include "ebtree/concept/tlog/tlog.h"
#include "ebtree/concept/wal/wal.h"
#include "ebtree/engine/shard_engine.h"
#include "ebtree/engine/routing_table.h"
#include "ebtree/engine/engine_attest.h"
#include "ebtree/engine/range_ticket.h"
#include "ebtree/engine/snapshot.h"
#include "ebtree/sync/sync_executor.h"

namespace ebtree {

class Engine {
 public:
  static Status Open(const EngineOptions& opts, std::unique_ptr<Engine>* out);

  Status Put(const std::string& key, const std::string& value,
             uint32_t txn_id = 0);
  Status Delete(const std::string& key, uint32_t txn_id = 0);
  Status Get(const std::string& key, std::string* value);
  SnapshotToken CaptureSnapshot();
  Status PinSnapshot(const SnapshotToken& token);
  void ReleaseSnapshot(const SnapshotToken& token);
  Status GetAtSnapshot(const std::string& key, const SnapshotToken& token,
                       uint32_t reader_txn_id, std::string* value);
  Status ScanAtSnapshot(const TypedPlan& plan, const SnapshotToken& token,
                        uint32_t reader_txn_id,
                        std::vector<std::pair<std::string, std::string>>* rows);

  Status ResolveLsnAtSnapshot(const std::string& key,
                              const SnapshotToken& token,
                              uint32_t reader_txn_id, uint64_t* lsn_out);
  Status ResolveCurrentCommittedLsn(const std::string& key, uint64_t* lsn_out);
  Status RefreshExternalWalForOCC();
  Status AppendTxnBegin(uint32_t txn_id, const SnapshotToken& token);
  Status AppendTxnCommit(uint32_t txn_id);
  Status AppendTxnAbort(uint32_t txn_id);
  void RegisterRangeTicket(uint32_t txn_id, uint64_t snapshot_lsn,
                           const std::string& key_lo, const std::string& key_hi);
  void UnregisterRangeTickets(uint32_t txn_id);
  Status ValidateRangeTicketCommit(
      uint32_t txn_id, uint64_t snapshot_lsn, const std::string& key_lo,
      const std::string& key_hi,
      const std::unordered_map<std::string, uint64_t>& write_set) const;

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
  Status CorruptWalForTest(uint32_t shard_id = 0);
  Status CorruptDataFileForTest(uint64_t record_offset, uint32_t shard_id = 0);
  Status TruncateWalForTest(uint32_t shard_id = 0);
  void ClearVcsForTest(uint32_t shard_id = 0);

  void SetCheckpointHookForTest(CheckpointHook hook);

  RecoveryMode recovery_mode() const;
  bool wal_replay_pending() const;

  EngineStats stats() const;
  EngineStats* mutable_stats();
  uint64_t stable_lsn() const;
  SyncExecutor* sync();
  SyncContext* sync_context();

  MemTable* memtable();
  MemTable* immutable_memtable();
  MemTable* flushing_memtable();
  MemTable* frozen_memtable();
  WalWriter* wal();
  DataFile* datafile();
  SuperBlockStore* superblock();
  TLogWriter* tlog();
  RegionManager* gc();
  BTreeIndex* btree();

  const SuperBlock& loaded_superblock() const;

  const std::unordered_map<std::string, std::pair<std::string, uint64_t>>&
  committed() const;

  DurabilityClass durability() const;
  const EngineOptions& options() const { return opts_; }
  uint32_t shard_count() const { return static_cast<uint32_t>(shards_.size()); }

  Status FlushInternal();
  Status CommitSuperBlockInternal();
  Status AppendTLogSnapshot();
  Status RepairSummary();
  void RotateMemTableForFlush();

  GroupCommitState* group_commit_state();

  std::unordered_map<std::string, std::pair<std::string, uint64_t>>&
  committed_mut();

  ShardEngine* shard(uint32_t id);
  const ShardEngine* shard(uint32_t id) const;
  ShardEngine* ShardForKey(const std::string& key);

  RecoverySnapshot GetRecoverySnapshot() const;
  void SetGroupCommitObserver(GroupCommitObserver observer);
  void SetCheckpointObserver(CheckpointObserver observer);
  void SetWriteGuard(WriteGuard guard);

 private:
  explicit Engine(EngineOptions opts);

  Status OpenInternal();
  Status EnsureShard(uint32_t id);
  Status EnsureShardForScan(uint32_t id);
  static void AccumulateStats(const EngineStats& src, EngineStats* dst);

  EngineOptions opts_;
  RoutingTable routing_table_{};
  std::vector<std::unique_ptr<ShardEngine>> shards_;
  GroupCommitObserver group_commit_observer_{};
  CheckpointObserver checkpoint_observer_{};
  WriteGuard write_guard_{};
  mutable std::mutex mu_;
  bool opened_{false};
};

}  // namespace ebtree
