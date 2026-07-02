#include "ebtree/engine/snapshot_resolver.h"

#include <fstream>
#include <functional>

#include "ebtree/concept/memtable/memtable.h"
#include "ebtree/concept/vcs/version_chain_store.h"
#include "ebtree/concept/wal/wal.h"
#include "ebtree/concept/wal/wal_segment.h"
#include "ebtree/engine/read_tier.h"
#include "ebtree/engine/sfs_read_cache.h"
#include "ebtree/engine/shard_engine.h"

namespace ebtree {

namespace {

std::optional<MemTableEntry> LookupMemTable(const MemTable& active,
                                            const MemTable& immutable,
                                            const MemTable& flushing,
                                            const std::string& key) {
  if (auto mt = active.Get(key)) return mt;
  if (auto mt = immutable.Get(key)) return mt;
  return flushing.Get(key);
}

bool TryWalSnapshotFloor(ShardEngine& shard, const std::string& key,
                         uint64_t snapshot_lsn, uint64_t* floor_lsn,
                         std::string* value, bool* deleted) {
  if (!floor_lsn || snapshot_lsn == 0) return false;
  if (shard.recovery_mode() == RecoveryMode::kHot) return false;
  if (!shard.wal_replay_pending() || shard.wal_corrupt() || !shard.wal()) {
    return false;
  }
  const uint64_t after = shard.loaded_superblock().critical.wal_lsn;
  const uint64_t best_lsn =
      WalSegmentReplayer::LatestCommittedLsnForKey(shard.wal()->path(), key,
                                                   after);
  if (best_lsn == 0 || best_lsn > snapshot_lsn) return false;
  *floor_lsn = best_lsn;
  if (value) {
    std::ifstream in(shard.wal()->path(), std::ios::binary);
    if (!in) return false;
    const uint64_t read_offset =
        WalSegmentReplayer::DataOffsetOnDisk(shard.wal()->path());
    in.seekg(static_cast<std::streamoff>(read_offset));
    while (in) {
      WalRecordHeaderV2 hdr{};
      std::string wal_key;
      std::string wal_value;
      in.read(reinterpret_cast<char*>(&hdr), sizeof(hdr));
      if (!in) break;
      wal_key.assign(hdr.key_len, '\0');
      wal_value.assign(hdr.value_len, '\0');
      if (hdr.key_len) in.read(wal_key.data(), hdr.key_len);
      if (hdr.value_len) in.read(wal_value.data(), hdr.value_len);
      if (!in) break;
      if (hdr.lsn != best_lsn || wal_key != key) continue;
      if (value && hdr.op == WalOp::kPut) *value = wal_value;
      if (deleted) *deleted = (hdr.op == WalOp::kDelete);
      return true;
    }
  }
  if (deleted) *deleted = false;
  return true;
}

}  // namespace

bool MemEntryVisible(const MemTableEntry& entry, const SnapshotReadContext& ctx) {
  if (ctx.reader_txn_id != 0 && entry.txn_id == ctx.reader_txn_id) {
    return true;
  }
  if (entry.durable) return true;
  if (ctx.snapshot_lsn > 0 && entry.lsn > ctx.snapshot_lsn) return false;
  return false;
}

Status SnapshotResolver::ResolveLsnAtSnapshot(ShardEngine& shard,
                                              const std::string& key,
                                              uint64_t snapshot_lsn,
                                              uint64_t* lsn_out) {
  if (!lsn_out) return Status::InvalidArgument("lsn_out is null");
  *lsn_out = 0;

  const uint64_t checkpoint_lsn = shard.loaded_superblock().critical.wal_lsn;
  const uint64_t s_epoch = SfsEpoch(snapshot_lsn, checkpoint_lsn);
  const uint64_t key_hash = std::hash<std::string>{}(key);
  if (const auto cached = shard.sfs_cache_.Lookup(key_hash, s_epoch)) {
    if (*cached == 0) return Status::NotFound("no version at snapshot");
    *lsn_out = *cached;
    return Status::Ok();
  }

  uint64_t forward = 0;
  if (shard.btree_.Get(key, &forward).ok()) {
    if (snapshot_lsn == 0 || forward <= snapshot_lsn) {
      *lsn_out = forward;
      shard.sfs_cache_.Insert(key_hash, s_epoch, forward, false);
      return Status::Ok();
    }
  }

  if (shard.vcs_) {
    const uint64_t floor_lsn = shard.vcs_->Floor(key, snapshot_lsn);
    if (floor_lsn > 0) {
      *lsn_out = floor_lsn;
      shard.sfs_cache_.Insert(key_hash, s_epoch, floor_lsn, false);
      return Status::Ok();
    }
  }

  uint64_t wal_floor = 0;
  std::string wal_value;
  bool wal_deleted = false;
  if (TryWalSnapshotFloor(shard, key, snapshot_lsn, &wal_floor, &wal_value,
                          &wal_deleted)) {
    shard.RecordTier(ReadTier::kWalSnapshotKey);
    if (wal_deleted) {
      shard.sfs_cache_.Insert(key_hash, s_epoch, 0, true);
      return Status::NotFound("deleted at snapshot");
    }
    *lsn_out = wal_floor;
    shard.sfs_cache_.Insert(key_hash, s_epoch, wal_floor, false);
    return Status::Ok();
  }

  if (!shard.vcs_) return Status::NotFound("no version at snapshot");
  return Status::NotFound("no version at snapshot");
}

namespace {

void ConsiderDurableMemLsn(const MemTableEntry& entry, uint64_t* best,
                           bool* found) {
  if (!entry.durable) return;
  if (entry.lsn >= *best) {
    *best = entry.lsn;
    *found = true;
  }
}

void ScanMemTableForDurableLsn(const MemTable& table, const std::string& key,
                               uint64_t* best, bool* found) {
  if (const auto entry = table.Get(key)) {
    ConsiderDurableMemLsn(*entry, best, found);
  }
}

}  // namespace

Status SnapshotResolver::ResolveCurrentCommittedLsn(ShardEngine& shard,
                                                    const std::string& key,
                                                    uint64_t* lsn_out) {
  if (!lsn_out) return Status::InvalidArgument("lsn_out is null");
  *lsn_out = 0;

  uint64_t best = 0;
  bool found = false;
  ScanMemTableForDurableLsn(shard.flushing_, key, &best, &found);
  ScanMemTableForDurableLsn(shard.immutable_, key, &best, &found);
  ScanMemTableForDurableLsn(shard.memtable_, key, &best, &found);

  const auto it = shard.committed_.find(key);
  if (it != shard.committed_.end() && it->second.second >= best) {
    best = it->second.second;
    found = true;
  }

  uint64_t forward = 0;
  if (shard.btree_.Get(key, &forward).ok() && forward > best) {
    best = forward;
    found = true;
  }

  if (shard.vcs_) {
    const uint64_t head = shard.vcs_->Head(key);
    if (head > best) {
      best = head;
      found = true;
    }
  }

  if (!found) return Status::NotFound("no committed version");
  *lsn_out = best;
  return Status::Ok();
}

Status SnapshotResolver::ResolveAtSnapshot(ShardEngine& shard,
                                           const std::string& key,
                                           const SnapshotReadContext& ctx,
                                           std::string* value) {
  if (!value) return Status::InvalidArgument("value is null");

  if (auto mt = LookupMemTable(shard.memtable_, shard.immutable_,
                               shard.flushing_, key)) {
    if (MemEntryVisible(*mt, ctx)) {
      if (mt->deleted) return Status::NotFound("deleted in memtable");
      shard.RecordTier(ReadTier::kMemTable);
      *value = mt->value;
      return Status::Ok();
    }
  }

  const auto it = shard.committed_.find(key);
  if (it != shard.committed_.end()) {
    if (ctx.snapshot_lsn == 0 || it->second.second <= ctx.snapshot_lsn) {
      shard.RecordTier(ReadTier::kCommitted);
      *value = it->second.first;
      return Status::Ok();
    }
  }

  uint64_t data_lsn = 0;
  const bool btree_hit = shard.btree_.Get(key, &data_lsn).ok();
  if (btree_hit &&
      (ctx.snapshot_lsn == 0 || data_lsn <= ctx.snapshot_lsn)) {
    shard.RecordTier(ReadTier::kBTreeDisk);
    const Status disk = shard.ReadValueByLsn(data_lsn, value);
    if (disk.ok()) return disk;
    if (disk.code() == StatusCode::kNotFound) return disk;
  }

  if (shard.vcs_) {
    const uint64_t floor_lsn =
        shard.vcs_->Floor(key, ctx.snapshot_lsn);
    if (floor_lsn > 0) {
      shard.RecordTier(ReadTier::kVersionChain);
      const Status disk = shard.ReadValueByLsn(floor_lsn, value);
      if (disk.ok()) return disk;
      if (disk.code() == StatusCode::kNotFound) return disk;
    }
  }

  uint64_t wal_floor = 0;
  std::string wal_value;
  bool wal_deleted = false;
  if (TryWalSnapshotFloor(shard, key, ctx.snapshot_lsn, &wal_floor, &wal_value,
                          &wal_deleted)) {
    shard.RecordTier(ReadTier::kWalSnapshotKey);
    if (wal_deleted) return Status::NotFound("deleted at snapshot");
    if (!wal_value.empty()) {
      *value = wal_value;
      return Status::Ok();
    }
    const Status disk = shard.ReadValueByLsn(wal_floor, value);
    if (disk.ok()) return disk;
    if (disk.code() == StatusCode::kNotFound) return disk;
  }

  if (btree_hit && ctx.snapshot_lsn > 0 && data_lsn > ctx.snapshot_lsn) {
    return Status::NotFound("not visible at snapshot");
  }
  return Status::NotFound("key not found");
}

}  // namespace ebtree
