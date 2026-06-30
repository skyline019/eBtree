#include "ebtree/engine/engine.h"

#include "ebtree/concept/tlog/tlog.h"
#include "ebtree/engine/read_tier.h"
#include "ebtree/engine/shard_router.h"
#include "ebtree/engine/routing_table.h"

#include <algorithm>
#include <atomic>
#include <filesystem>
#include <thread>
#include <vector>

namespace ebtree {

namespace {

void MergeScanRows(
    std::vector<std::vector<std::pair<std::string, std::string>>> shard_rows,
    std::vector<std::pair<std::string, std::string>>* out) {
  out->clear();
  size_t total = 0;
  for (const auto& rows : shard_rows) total += rows.size();
  out->reserve(total);
  for (auto& rows : shard_rows) {
    out->insert(out->end(), rows.begin(), rows.end());
  }
  std::sort(out->begin(), out->end(),
            [](const auto& a, const auto& b) { return a.first < b.first; });
}

bool ShardHasOnDiskState(const std::string& base, uint32_t shard_id) {
  const std::string prefix = base + "/shard" + std::to_string(shard_id);
  return std::filesystem::exists(prefix + ".super") ||
         std::filesystem::exists(prefix + ".wal") ||
         std::filesystem::exists(prefix + ".data");
}

}  // namespace

Engine::Engine(EngineOptions opts) : opts_(std::move(opts)) {}

Status Engine::Open(const EngineOptions& opts, std::unique_ptr<Engine>* out) {
  if (!out) return Status::InvalidArgument("out is null");
  const Status vc = ValidateShardCount(opts.shard_count);
  if (!vc.ok()) return vc;
  auto engine = std::unique_ptr<Engine>(new Engine(opts));
  const Status st = engine->OpenInternal();
  if (!st.ok()) return st;
  *out = std::move(engine);
  return Status::Ok();
}

Status Engine::OpenInternal() {
  std::lock_guard<std::mutex> lock(mu_);
  shards_.clear();
  shards_.resize(opts_.shard_count);
  routing_table_ = BuildRoutingTable(opts_.shard_count);
  opened_ = true;
  if (opts_.shard_count == 1) {
    std::unique_ptr<ShardEngine> shard;
    const Status st = ShardEngine::Create(0, opts_, &shard);
    if (!st.ok()) return st;
    shards_[0] = std::move(shard);
    return Status::Ok();
  }
  if (!opts_.eager_shard_open) return Status::Ok();

  std::vector<Status> statuses(opts_.shard_count, Status::Ok());
  std::vector<std::unique_ptr<ShardEngine>> created(opts_.shard_count);
  std::atomic<uint32_t> next{0};
  const unsigned workers = std::max(
      1u, std::min(static_cast<unsigned>(opts_.shard_count),
                   std::thread::hardware_concurrency()));
  std::vector<std::thread> pool;
  pool.reserve(workers);
  for (unsigned w = 0; w < workers; ++w) {
    pool.emplace_back([&]() {
      for (;;) {
        const uint32_t id = next.fetch_add(1);
        if (id >= opts_.shard_count) break;
        std::unique_ptr<ShardEngine> shard;
        statuses[id] = ShardEngine::Create(id, opts_, &shard);
        if (statuses[id].ok()) created[id] = std::move(shard);
      }
    });
  }
  for (auto& t : pool) t.join();
  for (const auto& st : statuses) {
    if (!st.ok()) return st;
  }
  for (uint32_t id = 0; id < opts_.shard_count; ++id) {
    shards_[id] = std::move(created[id]);
  }
  return Status::Ok();
}

Status Engine::EnsureShard(uint32_t id) {
  if (id >= shards_.size()) return Status::InvalidArgument("invalid shard");
  if (shards_[id]) return Status::Ok();
  std::lock_guard<std::mutex> lock(mu_);
  if (shards_[id]) return Status::Ok();
  std::unique_ptr<ShardEngine> shard;
  const Status st = ShardEngine::Create(id, opts_, &shard);
  if (!st.ok()) return st;
  shards_[id] = std::move(shard);
  opened_ = true;
  return Status::Ok();
}

Status Engine::EnsureShardForScan(uint32_t id) {
  if (id >= shards_.size()) return Status::InvalidArgument("invalid shard");
  if (shards_[id]) return Status::Ok();
  if (opts_.shard_count > 1 &&
      !ShardHasOnDiskState(opts_.path, id)) {
    return Status::Ok();
  }
  return EnsureShard(id);
}

ShardEngine* Engine::shard(uint32_t id) {
  if (id >= shards_.size()) return nullptr;
  return shards_[id].get();
}

const ShardEngine* Engine::shard(uint32_t id) const {
  if (id >= shards_.size()) return nullptr;
  return shards_[id].get();
}

ShardEngine* Engine::ShardForKey(const std::string& key) {
  uint32_t id = RouteShard(key, opts_.shard_count);
  if (opts_.shard_count == 256 && key.size() == 1) {
    id = routing_table_.slots[static_cast<unsigned char>(key[0])];
  }
  if (EnsureShard(id).ok()) return shard(id);
  return nullptr;
}

void Engine::AccumulateStats(const EngineStats& src, EngineStats* dst) {
  dst->fallback_read_total += src.fallback_read_total;
  dst->bypass_prepare_total += src.bypass_prepare_total;
  dst->wal_append_total += src.wal_append_total;
  dst->superblock_commit_total += src.superblock_commit_total;
  dst->stable_lsn = std::max(dst->stable_lsn, src.stable_lsn);
  dst->group_commit_total += src.group_commit_total;
  dst->flusher_flush_total += src.flusher_flush_total;
  dst->recovery_total += src.recovery_total;
  dst->lazy_page_faults += src.lazy_page_faults;
  dst->summary_repair_total += src.summary_repair_total;
  dst->tlog_snapshot_total += src.tlog_snapshot_total;
  dst->gc_region_swap_total += src.gc_region_swap_total;
  dst->wal_replay_deferred_total += src.wal_replay_deferred_total;
  dst->pages_touched += src.pages_touched;
  dst->wal_full_scan_total += src.wal_full_scan_total;
  dst->unexpected_path_total += src.unexpected_path_total;
  for (size_t i = 0; i < kReadTierCount; ++i) {
    dst->read_tier_hits[i] += src.read_tier_hits[i];
  }
  dst->fsync_batch_total += src.fsync_batch_total;
  dst->fsync_waiter_total += src.fsync_waiter_total;
  if (dst->fsync_batch_total > 0) {
    dst->fsync_merge_ratio = dst->fsync_waiter_total / dst->fsync_batch_total;
  }
}

EngineStats Engine::stats() const {
  EngineStats agg{};
  for (const auto& s : shards_) {
    if (!s) continue;
    if (shards_.size() == 1) return s->stats();
    AccumulateStats(s->stats(), &agg);
  }
  return agg;
}

EngineStats* Engine::mutable_stats() {
  if (!shards_.empty() && shards_[0]) return shards_[0]->mutable_stats();
  return nullptr;
}

RecoveryMode Engine::recovery_mode() const {
  if (shards_.empty() || !shards_[0]) return RecoveryMode::kHot;
  return shards_[0]->recovery_mode();
}

bool Engine::wal_replay_pending() const {
  for (const auto& s : shards_) {
    if (s && s->wal_replay_pending()) return true;
  }
  return false;
}

Status Engine::Put(const std::string& key, const std::string& value) {
  if (!opened_) return Status::Internal("engine not open");
  ShardEngine* s = ShardForKey(key);
  if (!s) return Status::Internal("shard missing");
  return s->Put(key, value);
}

Status Engine::Delete(const std::string& key) {
  if (!opened_) return Status::Internal("engine not open");
  ShardEngine* s = ShardForKey(key);
  if (!s) return Status::Internal("shard missing");
  return s->Delete(key);
}

Status Engine::Get(const std::string& key, std::string* value) {
  ShardEngine* s = ShardForKey(key);
  if (!s) return Status::Internal("shard missing");
  return s->Get(key, value);
}

Status Engine::Scan(const TypedPlan& plan,
                    std::vector<std::pair<std::string, std::string>>* rows) {
  if (!rows) return Status::InvalidArgument("rows is null");
  if (shards_.size() == 1) {
    if (EnsureShard(0).ok() && shards_[0]) {
      return shards_[0]->Scan(plan, rows);
    }
    return Status::Internal("shard missing");
  }

  std::vector<std::vector<std::pair<std::string, std::string>>> per_shard;
  per_shard.resize(shards_.size());
  std::vector<Status> statuses(shards_.size(), Status::Ok());
  const unsigned workers =
      std::max(1u, std::min(static_cast<unsigned>(shards_.size()),
                            std::thread::hardware_concurrency()));
  std::atomic<size_t> next{0};
  std::vector<std::thread> pool;
  pool.reserve(workers);
  for (unsigned w = 0; w < workers; ++w) {
    pool.emplace_back([&, plan]() {
      for (;;) {
        const size_t i = next.fetch_add(1);
        if (i >= shards_.size()) break;
        if (!EnsureShardForScan(static_cast<uint32_t>(i)).ok()) {
          statuses[i] = Status::Internal("ensure shard failed");
          continue;
        }
        if (!shards_[i]) continue;
        TypedPlan shard_plan = plan;
        const uint64_t shard_stable = shards_[i]->stats().stable_lsn;
        if (plan.snapshot_lsn > 0) {
          shard_plan.snapshot_lsn = std::min(plan.snapshot_lsn, shard_stable);
        } else if (shard_stable > 0) {
          shard_plan.snapshot_lsn = shard_stable;
        }
        statuses[i] = shards_[i]->Scan(shard_plan, &per_shard[i]);
      }
    });
  }
  for (auto& t : pool) t.join();
  for (const auto& st : statuses) {
    if (!st.ok()) return st;
  }
  MergeScanRows(std::move(per_shard), rows);
  return Status::Ok();
}

Status Engine::GetAsOf(const std::string& key, uint32_t timestamp_sec,
                       std::string* value) {
  ShardEngine* s = ShardForKey(key);
  if (!s) return Status::Internal("shard missing");
  return s->GetAsOf(key, timestamp_sec, value);
}

Status Engine::ScanAsOf(const TypedPlan& plan, uint32_t timestamp_sec,
                        std::vector<std::pair<std::string, std::string>>* rows) {
  if (!rows) return Status::InvalidArgument("rows is null");
  if (shards_.size() == 1) {
    if (EnsureShard(0).ok() && shards_[0]) {
      return shards_[0]->ScanAsOf(plan, timestamp_sec, rows);
    }
    return Status::Internal("shard missing");
  }

  std::vector<std::vector<std::pair<std::string, std::string>>> per_shard;
  per_shard.resize(shards_.size());
  for (size_t i = 0; i < shards_.size(); ++i) {
    if (!EnsureShardForScan(static_cast<uint32_t>(i)).ok()) {
      return Status::Internal("ensure shard failed");
    }
    if (!shards_[i]) continue;
    const Status st = shards_[i]->ScanAsOf(plan, timestamp_sec, &per_shard[i]);
    if (!st.ok()) return st;
  }
  MergeScanRows(std::move(per_shard), rows);
  return Status::Ok();
}

Status Engine::Prepare(const TypedPlan& plan) const {
  if (shards_.size() == 1) {
    if (shards_[0]) return shards_[0]->Prepare(plan);
    return Status::Ok();
  }
  if (plan.op == PredicateOp::kEq) {
    ShardEngine* s = const_cast<Engine*>(this)->ShardForKey(plan.key);
    if (!s) return Status::Internal("shard missing");
    return s->Prepare(plan);
  }
  for (const auto& s : shards_) {
    if (!s) continue;
    const Status st = s->Prepare(plan);
    if (!st.ok()) return st;
  }
  return Status::Ok();
}

Status Engine::Flush() {
  for (auto& s : shards_) {
    if (!s) continue;
    const Status st = s->Flush();
    if (!st.ok()) return st;
  }
  return Status::Ok();
}

Status Engine::Checkpoint() {
  BeginCheckpointTimestampScope();
  Status overall = Status::Ok();
  for (auto& s : shards_) {
    if (!s) continue;
    const Status st = s->Checkpoint();
    if (!st.ok()) {
      overall = st;
      break;
    }
  }
  EndCheckpointTimestampScope();
  return overall;
}

Status Engine::Recover() {
  for (auto& s : shards_) {
    if (!s) continue;
    const Status st = s->Recover();
    if (!st.ok()) return st;
  }
  return Status::Ok();
}

Status Engine::RecoverFull() {
  for (auto& s : shards_) {
    if (!s) continue;
    const Status st = s->RecoverFull();
    if (!st.ok()) return st;
  }
  return Status::Ok();
}

Status Engine::GroupCommit() {
  for (auto& s : shards_) {
    if (!s) continue;
    const Status st = s->GroupCommit();
    if (!st.ok()) return st;
  }
  if (group_commit_observer_) {
    group_commit_observer_(this);
  }
  return Status::Ok();
}

Status Engine::RecoverFromSuperBlock(const SuperBlock& sb) {
  if (shards_.empty()) return Status::Internal("no shards");
  return shards_[0]->RecoverFromSuperBlock(sb);
}

Status Engine::CorruptSuperBlockForTest() {
  return shards_[0]->CorruptSuperBlockForTest();
}

Status Engine::CorruptSuperBlockSlotForTest(int slot) {
  return shards_[0]->CorruptSuperBlockSlotForTest(slot);
}

Status Engine::CorruptRootForTest() { return shards_[0]->CorruptRootForTest(); }

Status Engine::CorruptWalForTest(uint32_t shard_id) {
  ShardEngine* s = shard(shard_id);
  if (!s) return Status::InvalidArgument("invalid shard");
  return s->CorruptWalForTest();
}

Status Engine::CorruptDataFileForTest(uint64_t record_offset,
                                      uint32_t shard_id) {
  ShardEngine* s = shard(shard_id);
  if (!s) return Status::InvalidArgument("invalid shard");
  return s->CorruptDataFileForTest(record_offset);
}

Status Engine::TruncateWalForTest(uint32_t shard_id) {
  ShardEngine* s = shard(shard_id);
  if (!s) return Status::InvalidArgument("invalid shard");
  return s->TruncateWalForTest();
}

void Engine::SetCheckpointHookForTest(CheckpointHook hook) {
  for (auto& s : shards_) {
    if (s) s->SetCheckpointHookForTest(hook);
  }
}

uint64_t Engine::stable_lsn() const { return stats().stable_lsn; }

SyncExecutor* Engine::sync() { return shards_[0]->sync(); }

SyncContext* Engine::sync_context() { return shards_[0]->sync_context(); }

MemTable* Engine::memtable() { return shards_[0]->memtable(); }

MemTable* Engine::immutable_memtable() { return shards_[0]->immutable_memtable(); }

MemTable* Engine::flushing_memtable() { return shards_[0]->flushing_memtable(); }

MemTable* Engine::frozen_memtable() { return shards_[0]->frozen_memtable(); }

WalWriter* Engine::wal() { return shards_[0]->wal(); }

DataFile* Engine::datafile() { return shards_[0]->datafile(); }

SuperBlockStore* Engine::superblock() { return shards_[0]->superblock(); }

TLogWriter* Engine::tlog() { return shards_[0]->tlog(); }

RegionManager* Engine::gc() { return shards_[0]->gc(); }

BTreeIndex* Engine::btree() { return shards_[0]->btree(); }

const SuperBlock& Engine::loaded_superblock() const {
  return shards_[0]->loaded_superblock();
}

const std::unordered_map<std::string, std::pair<std::string, uint64_t>>&
Engine::committed() const {
  return shards_[0]->committed();
}

DurabilityClass Engine::durability() const { return opts_.durability; }

Status Engine::FlushInternal() { return shards_[0]->FlushInternal(); }

Status Engine::CommitSuperBlockInternal() {
  return shards_[0]->CommitSuperBlockInternal();
}

Status Engine::AppendTLogSnapshot() { return shards_[0]->AppendTLogSnapshot(); }

Status Engine::RepairSummary() { return shards_[0]->RepairSummary(); }

void Engine::RotateMemTableForFlush() { shards_[0]->RotateMemTableForFlush(); }

GroupCommitState* Engine::group_commit_state() {
  return shards_[0]->group_commit_state();
}

std::unordered_map<std::string, std::pair<std::string, uint64_t>>&
Engine::committed_mut() {
  return shards_[0]->committed_mut();
}

RecoverySnapshot Engine::GetRecoverySnapshot() const {
  RecoverySnapshot snap{};
  snap.recovery_mode = recovery_mode();
  snap.wal_replay_pending = wal_replay_pending();
  snap.unexpected_path_total = stats().unexpected_path_total;
  snap.stable_lsn = stats().stable_lsn;
  const uint32_t n = shard_count();
  snap.shards.reserve(n);
  for (uint32_t i = 0; i < n; ++i) {
    RecoveryShardSnapshot shard_snap{};
    shard_snap.shard_id = i;
    const ShardEngine* se = shard(i);
    if (!se) {
      snap.shards.push_back(shard_snap);
      continue;
    }
    shard_snap.state = se->recovery_state();
    shard_snap.wal_corrupt = se->wal_corrupt();
    shard_snap.lazy_root_corrupt = se->lazy_root_corrupt();
    for (size_t t = 0; t < kReadTierCount; ++t) {
      shard_snap.read_tier_hits[t] = se->stats().read_tier_hits[t];
    }
    snap.shards.push_back(shard_snap);
  }
  return snap;
}

void Engine::SetGroupCommitObserver(GroupCommitObserver observer) {
  group_commit_observer_ = std::move(observer);
}

}  // namespace ebtree
