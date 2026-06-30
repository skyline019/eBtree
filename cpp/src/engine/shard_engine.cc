#include "ebtree/engine/shard_engine.h"
#include "ebtree/engine/read_resolver.h"
#include "ebtree/engine/scan_resolver.h"
#include "ebtree/engine/write_request.h"

#include "ebtree/concept/btree/summary_healer.h"
#include "ebtree/concept/flusher/flusher.h"
#include "ebtree/concept/wal/wal_segment.h"
#include "ebtree/engine/flush_worker.h"
#include "ebtree/engine/summary_validator.h"
#include "ebtree/concept/page/page_format.h"
#include <chrono>
#include <filesystem>
#include <fstream>
#include <optional>
#include <sstream>
#include <unordered_map>

namespace ebtree {

namespace {

std::string ShardPathPrefix(const std::string& base, uint32_t shard_id) {
  return base + "/shard" + std::to_string(shard_id);
}

std::optional<MemTableEntry> LookupMemTable(const MemTable& active,
                                            const MemTable& immutable,
                                            const MemTable& flushing,
                                            const std::string& key) {
  if (auto mt = active.Get(key)) return mt;
  if (auto mt = immutable.Get(key)) return mt;
  return flushing.Get(key);
}

void ApplyMemTableOverlay(const MemTable& mt, const TypedPlan& plan,
                          std::unordered_map<std::string, uint64_t>* merged) {
  for (const auto& kv : mt.Snapshot()) {
    if (plan.op == PredicateOp::kEq) {
      if (kv.first != plan.key) continue;
    } else if (plan.op == PredicateOp::kRange) {
      if (kv.first < plan.key || kv.first > plan.range_end) continue;
    } else {
      continue;
    }
    if (kv.second.deleted) {
      merged->erase(kv.first);
    } else {
      (*merged)[kv.first] = kv.second.lsn;
    }
  }
}

Status ReplayWalToMemTable(WalWriter* wal, MemTable* mt, uint64_t after_lsn) {
  return WalSegmentReplayer::ReplayPending(wal, mt, after_lsn);
}

bool HasUnreplayedWal(WalWriter* wal, uint64_t after_lsn) {
  return WalSegmentReplayer::HasPending(wal, after_lsn);
}

bool PlanMatchesKey(const TypedPlan& plan, const std::string& key) {
  if (plan.op == PredicateOp::kEq) return key == plan.key;
  if (plan.op == PredicateOp::kRange) {
    return key >= plan.key && key <= plan.range_end;
  }
  return false;
}

Status LoadDatafileMmap(MmapWindowManager* mmap_mgr, DataFile* datafile,
                        std::unordered_map<std::string,
                                           std::pair<std::string, uint64_t>>* out,
                        uint64_t* data_max, uint64_t lsn_cap, uint64_t byte_cap,
                        uint8_t reclaim) {
  const Status open_st = mmap_mgr->OpenReadOnly(datafile->path());
  if (!open_st.ok()) return open_st;
  const std::uintmax_t file_size = datafile->FileSize();
  if (file_size == 0) {
    if (out) out->clear();
    if (data_max) *data_max = 0;
    return Status::Ok();
  }

  uint64_t offset = 0;
  while (offset < file_size) {
    if (byte_cap > 0 && offset >= byte_cap) break;
    MmapView view{};
    const Status pin_st = mmap_mgr->PinWindow(offset, &view);
    if (!pin_st.ok()) return pin_st;
    size_t consumed = 0;
    Status ds = Status::Ok();
    if (view.size > 0) {
      ds = datafile->LoadRecordsIncremental(view.base, view.size, &consumed, offset,
                                            out, data_max, lsn_cap, byte_cap, reclaim);
    }
    mmap_mgr->Unpin();
    if (!ds.ok()) return ds;
    if (consumed == 0) {
      const size_t step =
          static_cast<size_t>(std::min<std::uintmax_t>(
              mmap_mgr->window_size(), file_size - offset));
      if (step == 0) break;
      offset += step;
    } else {
      offset += consumed;
    }
  }
  return Status::Ok();
}

}  // namespace

ShardEngine::ShardEngine(uint32_t shard_id, EngineOptions opts)
    : shard_id_(shard_id), opts_(std::move(opts)) {
  sync_ctx_.shard = this;
}

ShardEngine::~ShardEngine() {
  if (summary_validator_) summary_validator_->Stop();
  if (flush_worker_) flush_worker_->Stop();
}

Status ShardEngine::Create(uint32_t shard_id, const EngineOptions& opts,
                           std::unique_ptr<ShardEngine>* out) {
  if (!out) return Status::InvalidArgument("out is null");
  auto shard = std::unique_ptr<ShardEngine>(new ShardEngine(shard_id, opts));
  const Status st = shard->OpenInternal();
  if (!st.ok()) return st;
  *out = std::move(shard);
  return Status::Ok();
}

Status ShardEngine::OpenInternal() {
  std::filesystem::create_directories(opts_.path);
  const std::string prefix = ShardPathPrefix(opts_.path, shard_id_);
  wal_ = std::make_unique<WalWriter>(prefix + ".wal");
  if (opts_.durability == DurabilityClass::kBalanced) {
    wal_->SetWriteThrough(true);
  }
  datafile_ = std::make_unique<DataFile>(prefix + ".data");
  datafile_->SetCompressValues(opts_.compress_values);
  superblock_ = std::make_unique<SuperBlockStore>(prefix + ".super");
  tlog_ = std::make_unique<TLogWriter>(prefix + ".tlog");
  gc_ = std::make_unique<RegionManager>(prefix + ".gcmeta");
  (void)gc_->Load();
  btree_.InitPages(prefix + ".pages");
  if (auto* pf = btree_.page_file()) {
    pf->SetCacheCapacity(opts_.page_cache_capacity);
    pf->SetCompressPages(opts_.compress_pages);
  }
  btree_.SetPreferHistogramSummary(opts_.prefer_histogram_summary);
  WalFsyncConfig fsync_cfg{};
  fsync_cfg.max_batch_size = opts_.fsync_batch_size;
  fsync_cfg.max_wait_us = opts_.fsync_max_wait_us;
  fsync_cfg.wal_batch_bytes = opts_.wal_durable_batch_bytes;
  if (opts_.durability == DurabilityClass::kBalanced) {
    wal_batch_pipeline_ =
        std::make_unique<WalBatchPipeline>(wal_.get(), fsync_cfg);
    wal_batch_pipeline_->SetCommitHook(
        [this](const std::vector<WalBatchCommitItem>& items, EngineStats* stats,
               bool lock_held) {
          if (lock_held) return ApplyWalBatchUnlocked(items, stats);
          return ApplyWalBatchLocked(items, stats);
        });
  } else {
    fsync_coordinator_ =
        std::make_unique<WalFsyncCoordinator>(wal_.get(), fsync_cfg);
  }
  datafile_reader_ = DataFileReader(datafile_.get(), &mmap_mgr_);

  RegisterCoreSyncRules(&sync_);
  RegisterDurabilitySyncRules(&sync_);
  RegisterReadSyncRules(&sync_);
  RegisterRecoverySyncRules(&sync_);

  const Status rec = Recover();
  if (!rec.ok()) return rec;
  if (opts_.background_flush) {
    flush_worker_ = std::make_unique<BackgroundFlushWorker>(this);
    flush_worker_->Start();
  }
  if (opts_.background_summary_validate) {
    summary_validator_ = std::make_unique<BackgroundSummaryValidator>(this);
    summary_validator_->Start();
  }
  opened_ = true;
  return Status::Ok();
}

Status ShardEngine::Recover() {
  SyncContext ctx;
  ctx.shard = this;
  const Status st = sync_.Dispatch(SyncEventType::kRecovery, &ctx);
  if (st.ok()) stats_.recovery_total++;
  return st;
}

Status ShardEngine::LoadDatafileOnRecover(
    const SuperBlock& sb,
    std::unordered_map<std::string, std::pair<std::string, uint64_t>>* out,
    uint64_t* data_max) {
  const uint8_t reclaim =
      gc_ ? gc_->reclaim_generation() : static_cast<uint8_t>(0xFF);

  Status ds = LoadDatafileMmap(&mmap_mgr_, datafile_.get(), out, data_max, 0, 0,
                               reclaim);
  if (ds.ok()) return ds;

  TLogSnapshot snap{};
  (void)tlog_->LatestSnapshot(&snap);
  if (snap.datafile_size > 0) {
    return LoadDatafileMmap(&mmap_mgr_, datafile_.get(), out, data_max, 0,
                            snap.datafile_size, reclaim);
  }
  if (sb.critical.data_lsn > 0) {
    return LoadDatafileMmap(&mmap_mgr_, datafile_.get(), out, data_max,
                            sb.critical.data_lsn, 0, reclaim);
  }
  return ds;
}

Status ShardEngine::RecoverFromSuperBlock(const SuperBlock& sb) {
  sb_ = sb;
  stats_.stable_lsn = sb.critical.data_lsn;
  wal_corrupt_ = false;

  std::ifstream wal_probe(wal_->path(), std::ios::binary);
  if (wal_probe) {
    char tag[6]{};
    wal_probe.read(tag, 6);
    if (wal_probe && std::string(tag, 6) == "BADWAL") {
      wal_corrupt_ = true;
    }
  }

  std::unordered_map<std::string, std::pair<std::string, uint64_t>> data;
  uint64_t data_max = 0;
  Status ds;
  const uint8_t reclaim =
      gc_ ? gc_->reclaim_generation() : static_cast<uint8_t>(0xFF);

  if (wal_corrupt_) {
    TLogSnapshot snap{};
    (void)tlog_->LatestSnapshot(&snap);
    if (snap.datafile_size > 0) {
      ds = LoadDatafileMmap(&mmap_mgr_, datafile_.get(), &data, &data_max, 0,
                            snap.datafile_size, reclaim);
    } else if (sb.critical.data_lsn > 0) {
      ds = LoadDatafileMmap(&mmap_mgr_, datafile_.get(), &data, &data_max,
                            sb.critical.data_lsn, 0, reclaim);
    } else {
      ds = LoadDatafileMmap(&mmap_mgr_, datafile_.get(), &data, &data_max, 0, 0,
                            reclaim);
    }
    if (!ds.ok()) return ds;
    committed_ = std::move(data);
    stats_.stable_lsn = std::max(stats_.stable_lsn, sb.critical.data_lsn);
    std::map<std::string, uint64_t> btree_map;
    for (const auto& kv : committed_) {
      btree_map[kv.first] = kv.second.second;
    }
    if (sb.critical.active_root >= kPageFileHeaderSize) {
      const Status lr = btree_.LoadRoot(sb.critical.active_root);
      if (!lr.ok()) btree_.LoadFromMap(btree_map);
      else btree_.EnsureSummaryLsnAtLeast(stats_.stable_lsn);
    } else {
      btree_.LoadFromMap(btree_map);
    }
    stats_.pages_touched = btree_.pages_touched();
    if (sb.critical.epoch > 0 && sb.critical.active_root == 0) {
      lazy_root_corrupt_ = true;
      recovery_mode_ = RecoveryMode::kLazy;
    } else {
      lazy_root_corrupt_ = false;
      recovery_mode_ = RecoveryMode::kHot;
    }
    wal_replay_pending_ = false;
    RefreshRecoveryState();
    return Status::Ok();
  }

  if (opts_.lazy_committed_load) {
    committed_.clear();
    const Status idx = datafile_->BuildLsnIndex();
    if (!idx.ok()) return idx;
    (void)mmap_mgr_.OpenReadOnly(datafile_->path());
    auto heal_btree_from_datafile = [&]() -> Status {
      std::unordered_map<std::string, std::pair<std::string, uint64_t>> heal_data;
      uint64_t data_max = 0;
      const Status ds = LoadDatafileOnRecover(sb, &heal_data, &data_max);
      if (!ds.ok()) return ds;
      std::map<std::string, uint64_t> btree_map;
      for (const auto& kv : heal_data) {
        btree_map[kv.first] = kv.second.second;
      }
      btree_.LoadFromMap(btree_map);
      return Status::Ok();
    };
    if (sb.critical.active_root >= kPageFileHeaderSize) {
      const Status lr = btree_.LoadRoot(sb.critical.active_root);
      if (!lr.ok()) {
        const Status heal = heal_btree_from_datafile();
        if (!heal.ok()) return heal;
        lazy_root_corrupt_ = true;
        recovery_mode_ = RecoveryMode::kLazy;
      } else {
        btree_.EnsureSummaryLsnAtLeast(stats_.stable_lsn);
        lazy_root_corrupt_ = false;
        recovery_mode_ = RecoveryMode::kHot;
      }
    } else if (sb.critical.epoch > 0 && sb.critical.active_root == 0) {
      const Status heal = heal_btree_from_datafile();
      if (!heal.ok()) return heal;
      lazy_root_corrupt_ = true;
      recovery_mode_ = RecoveryMode::kLazy;
    } else {
      lazy_root_corrupt_ = false;
      recovery_mode_ = RecoveryMode::kHot;
    }
    stats_.pages_touched = btree_.pages_touched();
    if (opts_.recovery_strategy == RecoveryStrategy::kFullReplay) {
      wal_replay_pending_ = false;
      const Status fr =
          ReplayWalToMemTable(wal_.get(), &memtable_, sb.critical.wal_lsn);
      RefreshRecoveryState();
      return fr;
    }
    wal_replay_pending_ = HasUnreplayedWal(wal_.get(), sb.critical.wal_lsn);
    RefreshRecoveryState();
    return Status::Ok();
  }

  ds = LoadDatafileOnRecover(sb, &data, &data_max);
  if (!ds.ok()) return ds;
  committed_ = std::move(data);
  stats_.stable_lsn = std::max(stats_.stable_lsn, sb.critical.data_lsn);
  std::map<std::string, uint64_t> btree_map;
  for (const auto& kv : committed_) {
    btree_map[kv.first] = kv.second.second;
  }
  if (sb.critical.active_root >= kPageFileHeaderSize) {
    const Status lr = btree_.LoadRoot(sb.critical.active_root);
    if (!lr.ok()) btree_.LoadFromMap(btree_map);
    else btree_.EnsureSummaryLsnAtLeast(stats_.stable_lsn);
  } else {
    btree_.LoadFromMap(btree_map);
  }
  stats_.pages_touched = btree_.pages_touched();

  if (sb.critical.epoch > 0 && sb.critical.active_root == 0) {
    lazy_root_corrupt_ = true;
    recovery_mode_ = RecoveryMode::kLazy;
  } else {
    lazy_root_corrupt_ = false;
    recovery_mode_ = RecoveryMode::kHot;
  }

  if (opts_.recovery_strategy == RecoveryStrategy::kFullReplay) {
    wal_replay_pending_ = false;
    const Status fr =
        ReplayWalToMemTable(wal_.get(), &memtable_, sb.critical.wal_lsn);
    RefreshRecoveryState();
    return fr;
  }

  wal_replay_pending_ = HasUnreplayedWal(wal_.get(), sb.critical.wal_lsn);
  RefreshRecoveryState();
  return Status::Ok();
}

void ShardEngine::RefreshRecoveryState() {
  RecoveryStateInput in;
  in.wal_corrupt = wal_corrupt_;
  in.wal_replay_pending = wal_replay_pending_;
  in.lazy_committed_load = opts_.lazy_committed_load;
  in.committed_empty = committed_.empty();
  in.memtables_empty = MemTablesEmpty();
  in.recovery_mode = recovery_mode_;
  in.lazy_root_corrupt = lazy_root_corrupt_;
  in.btree_on_disk = btree_.on_disk_mode();
  recovery_state_ = ComputeRecoveryState(in);
}

void ShardEngine::RecordTier(ReadTier tier) const {
  RecordReadTier(tier, stats_.read_tier_hits, &stats_.unexpected_path_total);
}

Status ShardEngine::RecoverFull() {
  std::unique_lock<std::shared_mutex> lock(rw_mu_);
  wal_replay_pending_ = false;
  return EnsureWalReplayed();
}

Status ShardEngine::EnsureWalReplayed() {
  if (!wal_replay_pending_ || wal_corrupt_) return Status::Ok();
  if (!wal_) return Status::Ok();

  const uint64_t after = sb_.critical.wal_lsn;
  const Status st = WalSegmentReplayer::ReplayPending(wal_.get(), &memtable_, after);
  if (!st.ok()) return st;
  wal_replay_pending_ = false;
  RefreshRecoveryState();
  stats_.wal_replay_deferred_total++;
  return Status::Ok();
}

Status ShardEngine::RestoreKeyFromWal(const std::string& key) {
  if (wal_corrupt_ || !wal_) return Status::NotFound("wal unavailable");
  const uint64_t after = sb_.critical.wal_lsn;
  uint64_t offset = 0;
  if (!wal_->key_index().Lookup(key, after, &offset)) {
    return Status::NotFound("key not in wal");
  }
  WalOp last_op = WalOp::kPut;
  std::string last_value;
  std::string key_copy = key;
  uint64_t last_lsn = 0;
  const Status rs =
      wal_->ReplayRecordAt(offset, &last_op, &key_copy, &last_value, &last_lsn);
  if (!rs.ok()) {
    return rs;
  }
  lazy_state_.lazy_page_faults++;
  stats_.lazy_page_faults++;
  if (last_op == WalOp::kDelete) {
    return memtable_.DeleteKey(key, last_lsn);
  }
  return memtable_.Put(key, last_value, last_lsn);
}

Status ShardEngine::RepairSummary() {
  const Status st = SummaryHealer::RebuildFromCommitted(&btree_, committed_);
  if (st.ok()) {
    btree_.EnsureSummaryLsnAtLeast(stats_.stable_lsn);
    stats_.summary_repair_total++;
  }
  return st;
}

bool ShardEngine::TryRepairSummaryIfDrifted() {
  std::unique_lock<std::shared_mutex> lock(rw_mu_);
  if (!btree_.SummaryDrifted()) return false;
  return RepairSummary().ok();
}

Status ShardEngine::BTreeScanWithHeal(
    const TypedPlan& plan,
    std::vector<std::pair<std::string, uint64_t>>* hits) {
  if (!hits) return Status::InvalidArgument("hits is null");
  Status st = btree_.Scan(plan, hits);
  if (st == StatusCode::kStaleSummary) {
    const Status heal = RepairSummary();
    if (!heal.ok()) return heal;
    hits->clear();
    st = btree_.Scan(plan, hits);
  }
  if (st == StatusCode::kStaleSummary) {
    hits->clear();
    st = Status::Ok();
  }
  return st;
}

Status ShardEngine::PutSyncFastAppend(const std::string& key,
                                      const std::string& value, uint64_t* lsn) {
  if (!lsn) return Status::InvalidArgument("lsn is null");
  const Status wal_st = wal_->Append(WalOp::kPut, key, value, lsn);
  if (!wal_st.ok()) return wal_st;
  stats_.wal_append_total++;
  return memtable_.Put(key, value, *lsn);
}

Status ShardEngine::DeleteSyncFastAppend(const std::string& key, uint64_t* lsn) {
  if (!lsn) return Status::InvalidArgument("lsn is null");
  const Status wal_st = wal_->Append(WalOp::kDelete, key, "", lsn);
  if (!wal_st.ok()) return wal_st;
  stats_.wal_append_total++;
  (void)memtable_.DeleteKey(key, *lsn);
  return Status::Ok();
}

Status ShardEngine::ApplyWalBatchUnlocked(
    const std::vector<WalBatchCommitItem>& items, EngineStats* stats) {
  uint64_t max_lsn = stats_.stable_lsn;
  for (const WalBatchCommitItem& item : items) {
    if (!item.key) return Status::Internal("batch commit missing key");
    Status st;
    if (item.op == WalOp::kDelete) {
      st = memtable_.DeleteKey(*item.key, item.lsn);
    } else {
      if (!item.value) return Status::Internal("batch commit missing value");
      st = memtable_.Put(*item.key, *item.value, item.lsn);
    }
    if (!st.ok()) return st;
    if (item.lsn > max_lsn) max_lsn = item.lsn;
  }
  stats_.stable_lsn = max_lsn;
  if (stats) stats->stable_lsn = max_lsn;
  return Status::Ok();
}

Status ShardEngine::ApplyWalBatchLocked(
    const std::vector<WalBatchCommitItem>& items, EngineStats* stats) {
  std::unique_lock<std::shared_mutex> lock(rw_mu_);
  return ApplyWalBatchUnlocked(items, stats);
}

Status ShardEngine::PutSyncFast(const std::string& key, const std::string& value) {
  uint64_t lsn = 0;
  const Status prep = PutSyncFastAppend(key, value, &lsn);
  if (!prep.ok()) return prep;
  const Status fs = fsync_coordinator_->Await(lsn, &stats_);
  if (!fs.ok()) return fs;
  stats_.stable_lsn = lsn;
  return Status::Ok();
}

Status ShardEngine::DeleteSyncFast(const std::string& key) {
  uint64_t lsn = 0;
  const Status prep = DeleteSyncFastAppend(key, &lsn);
  if (!prep.ok()) return prep;
  const Status fs = fsync_coordinator_->Await(lsn, &stats_);
  if (!fs.ok()) return fs;
  stats_.stable_lsn = lsn;
  return Status::Ok();
}

Status ShardEngine::Put(const std::string& key, const std::string& value) {
  if (!opened_) return Status::Internal("engine not open");
  if (key.empty()) return Status::InvalidArgument("InvalidPlan: empty key");
  if (opts_.durability == DurabilityClass::kBalanced && wal_batch_pipeline_) {
    {
      std::shared_lock<std::shared_mutex> lock(rw_mu_);
      const Status replay = EnsureWalReplayed();
      if (!replay.ok()) return replay;
    }
    const Status st = wal_batch_pipeline_->Put(key, value, nullptr, &stats_);
    if (!st.ok()) return st;
    if (flush_worker_) flush_worker_->Notify();
    return Status::Ok();
  }
  const bool sync_path =
      opts_.durability == DurabilityClass::kSync || opts_.sync_on_commit;
  uint64_t lsn = 0;
  {
    std::unique_lock<std::shared_mutex> lock(rw_mu_);
    const Status replay = EnsureWalReplayed();
    if (!replay.ok()) return replay;
    if (sync_path) {
      const Status st = PutSyncFastAppend(key, value, &lsn);
      if (!st.ok()) return st;
    } else {
      g_write_req = WriteRequest{key, value, false};
      SyncContext ctx;
      ctx.shard = this;
      const Status st = sync_.Dispatch(SyncEventType::kWrite, &ctx);
      if (st.ok() && flush_worker_) flush_worker_->Notify();
      return st;
    }
  }
  const Status fs = fsync_coordinator_->Await(lsn, &stats_);
  if (!fs.ok()) return fs;
  {
    std::unique_lock<std::shared_mutex> lock(rw_mu_);
    stats_.stable_lsn = lsn;
  }
  if (flush_worker_) flush_worker_->Notify();
  return Status::Ok();
}

Status ShardEngine::Delete(const std::string& key) {
  if (!opened_) return Status::Internal("engine not open");
  if (key.empty()) return Status::InvalidArgument("InvalidPlan: empty key");
  if (opts_.durability == DurabilityClass::kBalanced && wal_batch_pipeline_) {
    {
      std::shared_lock<std::shared_mutex> lock(rw_mu_);
      const Status replay = EnsureWalReplayed();
      if (!replay.ok()) return replay;
    }
    const Status st = wal_batch_pipeline_->Delete(key, nullptr, &stats_);
    if (!st.ok()) return st;
    if (flush_worker_) flush_worker_->Notify();
    return Status::Ok();
  }
  const bool sync_path =
      opts_.durability == DurabilityClass::kSync || opts_.sync_on_commit;
  uint64_t lsn = 0;
  {
    std::unique_lock<std::shared_mutex> lock(rw_mu_);
    const Status replay = EnsureWalReplayed();
    if (!replay.ok()) return replay;
    if (sync_path) {
      const Status st = DeleteSyncFastAppend(key, &lsn);
      if (!st.ok()) return st;
    } else {
      g_write_req = WriteRequest{key, "", true};
      SyncContext ctx;
      ctx.shard = this;
      const Status st = sync_.Dispatch(SyncEventType::kWrite, &ctx);
      if (st.ok() && flush_worker_) flush_worker_->Notify();
      return st;
    }
  }
  const Status fs = fsync_coordinator_->Await(lsn, &stats_);
  if (!fs.ok()) return fs;
  {
    std::unique_lock<std::shared_mutex> lock(rw_mu_);
    stats_.stable_lsn = lsn;
  }
  if (flush_worker_) flush_worker_->Notify();
  return Status::Ok();
}

Status ShardEngine::ReadValueByLsn(uint64_t lsn, std::string* value) {
  if (!value || !datafile_) return Status::InvalidArgument("invalid read by lsn");
  const uint8_t reclaim =
      gc_ ? gc_->reclaim_generation() : static_cast<uint8_t>(0xFF);
  RecordTier(ReadTier::kDataFileLsn);
  return datafile_reader_.ReadByLsn(lsn, value, reclaim);
}

Status ShardEngine::ResolveScanValues(
    const std::vector<std::pair<std::string, uint64_t>>& hits,
    uint64_t snapshot_lsn,
    std::vector<std::pair<std::string, std::string>>* rows) {
  if (!rows) return Status::InvalidArgument("rows is null");
  rows->clear();
  rows->reserve(hits.size());
  const bool disk_values =
      opts_.lazy_committed_load || btree_.on_disk_mode();
  const bool direct_disk_scan =
      disk_values && MemTablesEmpty() && committed_.empty();

  if (!disk_values && !committed_.empty()) {
    for (const auto& hit : hits) {
      if (auto mt = LookupMemTable(memtable_, immutable_, flushing_, hit.first)) {
        if (mt->deleted) continue;
        if (snapshot_lsn > 0 && mt->lsn > snapshot_lsn) continue;
        rows->emplace_back(hit.first, mt->value);
        continue;
      }
      const auto it = committed_.find(hit.first);
      if (it == committed_.end()) continue;
      if (snapshot_lsn > 0 && it->second.second > snapshot_lsn) continue;
      rows->emplace_back(hit.first, it->second.first);
    }
    return Status::Ok();
  }

  std::vector<std::pair<std::string, std::string>> partial;
  partial.reserve(hits.size());
  struct DiskEntry {
    uint64_t offset;
    size_t partial_idx;
    uint64_t lsn;
  };
  std::vector<DiskEntry> disk_batch;
  disk_batch.reserve(hits.size());

  for (const auto& hit : hits) {
    if (snapshot_lsn > 0 && hit.second > snapshot_lsn) continue;
    if (!direct_disk_scan) {
      std::string value;
      const Status rs = ReadVisible(hit.first, &value, snapshot_lsn);
      if (rs.ok()) {
        partial.emplace_back(hit.first, std::move(value));
        continue;
      }
      if (!disk_values) continue;
    }
    uint64_t offset = 0;
    if (!datafile_->lsn_index().Lookup(hit.second, &offset)) continue;
    const size_t row_idx = partial.size();
    partial.emplace_back(hit.first, std::string{});
    disk_batch.push_back({offset, row_idx, hit.second});
  }

  if (!disk_batch.empty()) {
    std::vector<std::string> disk_values_vec(partial.size());
    const uint8_t reclaim =
        gc_ ? gc_->reclaim_generation() : static_cast<uint8_t>(0xFF);
    std::vector<std::pair<uint64_t, size_t>> batch_pairs;
    batch_pairs.reserve(disk_batch.size());
    for (const auto& entry : disk_batch) {
      batch_pairs.emplace_back(entry.offset, entry.partial_idx);
    }
    const Status batch =
        datafile_reader_.ReadBatch(batch_pairs, &disk_values_vec, reclaim);
    if (batch.ok()) {
      for (const auto& entry : disk_batch) {
        if (entry.partial_idx < partial.size() &&
            !disk_values_vec[entry.partial_idx].empty()) {
          partial[entry.partial_idx].second =
              std::move(disk_values_vec[entry.partial_idx]);
        }
      }
    } else {
      for (const auto& entry : disk_batch) {
        std::string value;
        if (ReadValueByLsn(entry.lsn, &value).ok()) {
          partial[entry.partial_idx].second = std::move(value);
        }
      }
    }
  }

  for (auto& row : partial) {
    if (!row.second.empty()) rows->push_back(std::move(row));
  }
  return Status::Ok();
}

Status ShardEngine::Get(const std::string& key, std::string* value) {
  return ReadResolver::Get(*this, key, value);
}

Status ShardEngine::Scan(const TypedPlan& plan,
                         std::vector<std::pair<std::string, std::string>>* rows) {
  return ScanResolver::Scan(*this, plan, rows);
}

Status ShardEngine::LoadAsOfCommitted(
    uint32_t timestamp_sec,
    std::unordered_map<std::string, std::pair<std::string, uint64_t>>* out,
    uint64_t* max_lsn) {
  if (!out) return Status::InvalidArgument("out is null");
  TLogReader reader(tlog_->path());
  TLogSnapshot snap{};
  const Status fs = reader.FindSnapshotAt(timestamp_sec, &snap);
  if (!fs.ok()) return fs;
  return LoadDatafileMmap(&mmap_mgr_, datafile_.get(), out, max_lsn, 0,
                          snap.datafile_size, kDataFileGenerationAll);
}

Status ShardEngine::GetAsOf(const std::string& key, uint32_t timestamp_sec,
                            std::string* value) {
  if (!value) return Status::InvalidArgument("value is null");
  if (key.empty()) return Status::InvalidArgument("InvalidPlan: empty key");
  std::shared_lock<std::shared_mutex> lock(rw_mu_);
  RecordTier(ReadTier::kTLogFlashback);

  std::unordered_map<std::string, std::pair<std::string, uint64_t>> asof;
  uint64_t max_lsn = 0;
  const Status ls = LoadAsOfCommitted(timestamp_sec, &asof, &max_lsn);
  if (!ls.ok()) return ls;
  const auto it = asof.find(key);
  if (it == asof.end()) return Status::NotFound("key not found at timestamp");
  *value = it->second.first;
  return Status::Ok();
}

Status ShardEngine::ScanAsOf(const TypedPlan& plan, uint32_t timestamp_sec,
                             std::vector<std::pair<std::string, std::string>>* rows) {
  if (!rows) return Status::InvalidArgument("rows is null");
  std::shared_lock<std::shared_mutex> lock(rw_mu_);
  RecordTier(ReadTier::kTLogFlashback);

  std::unordered_map<std::string, std::pair<std::string, uint64_t>> asof;
  uint64_t max_lsn = 0;
  const Status ls = LoadAsOfCommitted(timestamp_sec, &asof, &max_lsn);
  if (!ls.ok()) return ls;

  rows->clear();
  for (const auto& kv : asof) {
    if (!PlanMatchesKey(plan, kv.first)) continue;
    rows->emplace_back(kv.first, kv.second.first);
  }
  std::sort(rows->begin(), rows->end(),
            [](const auto& a, const auto& b) { return a.first < b.first; });
  return Status::Ok();
}

bool ShardEngine::MemTablesEmpty() const {
  return memtable_.Empty() && immutable_.Empty() && flushing_.Empty();
}

bool ShardEngine::CanScanCommittedDirect(const TypedPlan& plan) const {
  if (recovery_state_ != ShardRecoveryState::kCommittedCold) return false;
  if (plan.op != PredicateOp::kRange && plan.op != PredicateOp::kEq) return false;
  if (plan.snapshot_lsn > 0 &&
      btree_.summary().summary_lsn < plan.snapshot_lsn) {
    return false;
  }
  return true;
}

Status ShardEngine::ScanCommittedDirect(
    const TypedPlan& plan, uint64_t snapshot_lsn,
    std::vector<std::pair<std::string, std::string>>* rows) const {
  if (!rows) return Status::InvalidArgument("rows is null");
  rows->clear();
  if (plan.op == PredicateOp::kEq) {
    const auto it = committed_.find(plan.key);
    if (it != committed_.end()) {
      if (snapshot_lsn == 0 || it->second.second <= snapshot_lsn) {
        rows->emplace_back(it->first, it->second.first);
      }
    }
    return Status::Ok();
  }
  rows->reserve(committed_.size());
  for (const auto& kv : committed_) {
    if (kv.first < plan.key || kv.first > plan.range_end) continue;
    if (snapshot_lsn > 0 && kv.second.second > snapshot_lsn) continue;
    rows->emplace_back(kv.first, kv.second.first);
  }
  if (rows->size() > 1) {
    std::sort(rows->begin(), rows->end(),
              [](const auto& a, const auto& b) { return a.first < b.first; });
  }
  return Status::Ok();
}

void ShardEngine::OverlayMemTableHits(
    const TypedPlan& plan,
    std::vector<std::pair<std::string, uint64_t>>* hits) const {
  if (!hits || MemTablesEmpty()) return;
  std::unordered_map<std::string, uint64_t> merged;
  merged.reserve(hits->size() + 8);
  for (const auto& hit : *hits) {
    merged[hit.first] = hit.second;
  }
  ApplyMemTableOverlay(flushing_, plan, &merged);
  ApplyMemTableOverlay(immutable_, plan, &merged);
  ApplyMemTableOverlay(memtable_, plan, &merged);
  hits->clear();
  hits->reserve(merged.size());
  for (const auto& kv : merged) {
    hits->emplace_back(kv.first, kv.second);
  }
  std::sort(hits->begin(), hits->end(),
            [](const auto& a, const auto& b) { return a.first < b.first; });
}

void ShardEngine::OverlayCommittedHits(
    const TypedPlan& plan, uint64_t snapshot_lsn,
    std::vector<std::pair<std::string, uint64_t>>* hits) const {
  (void)snapshot_lsn;
  if (!hits) return;
  std::unordered_map<std::string, uint64_t> merged;
  merged.reserve(hits->size() + committed_.size());
  for (const auto& hit : *hits) merged[hit.first] = hit.second;
  for (const auto& kv : committed_) {
    if (!PlanMatchesKey(plan, kv.first)) continue;
    merged[kv.first] = kv.second.second;
  }
  hits->clear();
  hits->reserve(merged.size());
  for (const auto& kv : merged) {
    hits->emplace_back(kv.first, kv.second);
  }
  std::sort(hits->begin(), hits->end(),
            [](const auto& a, const auto& b) { return a.first < b.first; });
}

Status ShardEngine::Prepare(const TypedPlan& plan) const {
  if (plan.op == PredicateOp::kEq && plan.key.empty()) {
    return Status::InvalidArgument("InvalidPlan: empty key");
  }
  if (plan.op == PredicateOp::kRange && plan.range_end < plan.key) {
    return Status::InvalidArgument("InvalidPlan: range_end before key");
  }
  if (plan.snapshot_lsn == 0 &&
      (plan.op == PredicateOp::kEq || plan.op == PredicateOp::kRange)) {
    stats_.bypass_prepare_total++;
  }
  if (plan.op != PredicateOp::kEq && plan.snapshot_lsn > 0 &&
      btree_.summary().summary_lsn < plan.snapshot_lsn) {
    return Status::StaleSummary("summary behind snapshot");
  }
  return Status::Ok();
}

Status ShardEngine::ReadVisible(const std::string& key, std::string* value,
                                uint64_t snapshot_lsn) {
  if (auto mt = LookupMemTable(memtable_, immutable_, flushing_, key)) {
    if (mt->deleted) return Status::NotFound("deleted in memtable");
    RecordTier(ReadTier::kMemTable);
    *value = mt->value;
    return Status::Ok();
  }
  const auto it = committed_.find(key);
  if (it == committed_.end()) return Status::NotFound("key not found");
  if (snapshot_lsn > 0 && it->second.second > snapshot_lsn) {
    return Status::NotFound("not visible at snapshot");
  }
  RecordTier(ReadTier::kCommitted);
  *value = it->second.first;
  return Status::Ok();
}

void ShardEngine::RotateMemTableForFlush() {
  memtable_.Swap(&immutable_);
  immutable_.Swap(&flushing_);
}

Status ShardEngine::Flush() {
  std::unique_lock<std::shared_mutex> lock(rw_mu_);
  const Status replay = EnsureWalReplayed();
  if (!replay.ok()) return replay;
  if (wal_batch_pipeline_) {
    const Status fp = wal_batch_pipeline_->FlushPending(&stats_);
    if (!fp.ok()) return fp;
  } else if (fsync_coordinator_) {
    const Status fp = fsync_coordinator_->FlushPending(&stats_);
    if (!fp.ok()) return fp;
  }
  RotateMemTableForFlush();
  SyncContext ctx;
  ctx.shard = this;
  return sync_.Dispatch(SyncEventType::kFlush, &ctx);
}

Status ShardEngine::GroupCommit() {
  std::unique_lock<std::shared_mutex> lock(rw_mu_);
  SyncContext ctx;
  ctx.shard = this;
  return sync_.Dispatch(SyncEventType::kGroupCommit, &ctx);
}

Status ShardEngine::Checkpoint() {
  std::unique_lock<std::shared_mutex> lock(rw_mu_);
  const Status replay = EnsureWalReplayed();
  if (!replay.ok()) return replay;
  if (wal_batch_pipeline_) {
    const Status fp = wal_batch_pipeline_->FlushPending(&stats_);
    if (!fp.ok()) return fp;
  } else if (fsync_coordinator_) {
    const Status fp = fsync_coordinator_->FlushPending(&stats_);
    if (!fp.ok()) return fp;
  }
  if (durability() == DurabilityClass::kGroup) {
    SyncContext gc_ctx;
    gc_ctx.shard = this;
    const Status gc = sync_.Dispatch(SyncEventType::kGroupCommit, &gc_ctx);
    if (!gc.ok()) return gc;
  }
  SyncContext fl_ctx;
  fl_ctx.shard = this;
  RotateMemTableForFlush();
  const Status fl = sync_.Dispatch(SyncEventType::kFlush, &fl_ctx);
  if (!fl.ok()) return fl;
  if (checkpoint_hook_ && checkpoint_hook_(CheckpointPhase::AfterFlush)) {
    return Status::Internal("checkpoint interrupted");
  }
  const Status tl = AppendTLogSnapshot();
  if (!tl.ok()) return tl;
  if (checkpoint_hook_ && checkpoint_hook_(CheckpointPhase::AfterTLog)) {
    return Status::Internal("checkpoint interrupted");
  }
  SyncContext sb_ctx;
  sb_ctx.shard = this;
  if (checkpoint_hook_ && checkpoint_hook_(CheckpointPhase::BeforeSuperBlock)) {
    return Status::Internal("checkpoint interrupted");
  }
  const Status sb_st = sync_.Dispatch(SyncEventType::kSuperBlockCommit, &sb_ctx);
  if (sb_st.ok()) {
    (void)mmap_mgr_.RotateEpoch();
    (void)datafile_->SaveLsnIndexSidecar();
    if (wal_ && sb_.critical.wal_lsn > 0) {
      (void)wal_->TruncateTo(sb_.critical.wal_lsn);
    }
    RefreshRecoveryState();
  }
  if (checkpoint_hook_ && checkpoint_hook_(CheckpointPhase::AfterSuperBlock)) {
    return Status::Internal("checkpoint interrupted");
  }
  return sb_st;
}

void ShardEngine::SetCheckpointHookForTest(CheckpointHook hook) {
  checkpoint_hook_ = std::move(hook);
}

Status ShardEngine::AppendTLogSnapshot() {
  if (!tlog_ || !datafile_) return Status::Internal("tlog not ready");
  uint64_t tail = 0;
  const Status st = tlog_->AppendSnapshot(stats_.stable_lsn, datafile_->FileSize(),
                                          wal_->max_lsn(), &tail);
  if (st.ok()) {
    stats_.tlog_snapshot_total++;
    sb_.tlog_tail = tail;
  }
  return st;
}

Status ShardEngine::CommitSuperBlockInternal() {
  SuperBlock sb{};
  (void)superblock_->Load(&sb);
  sb.critical.data_lsn = stats_.stable_lsn;
  if (lazy_root_corrupt_) {
    sb.critical.wal_lsn = sb.critical.wal_lsn;
    sb.critical.active_root = 0;
  } else {
    sb.critical.wal_lsn = wal_->max_lsn();
    uint64_t root_off = kLegacyMapRoot;
    std::map<std::string, uint64_t> btree_map;
    for (const auto& kv : committed_) {
      btree_map[kv.first] = kv.second.second;
    }
    btree_.RebuildSummaryFromCommitted(btree_map);
    btree_.EnsureSummaryLsnAtLeast(stats_.stable_lsn);
    const Status ps = btree_.PersistRootFromMap(btree_map, &root_off);
    if (!ps.ok()) return ps;
    const Status lr = btree_.LoadRoot(root_off);
    if (!lr.ok()) return lr;
    btree_.EnsureSummaryLsnAtLeast(stats_.stable_lsn);
    sb.critical.active_root = root_off;
  }
  sb.tlog_tail = sb_.tlog_tail;
  const Status st = superblock_->Commit(sb);
  if (st.ok()) stats_.superblock_commit_total++;
  sb_ = sb;
  return st;
}

Status ShardEngine::FlushInternal() {
  (void)MaybeGcSwap();
  FlusherContext ctx{};
  ctx.wal = wal_.get();
  ctx.frozen = &flushing_;
  ctx.datafile = datafile_.get();
  ctx.btree = &btree_;
  ctx.committed = &committed_;
  ctx.stats = &stats_;
  ctx.generation = gc_ ? gc_->active_generation() : 0;
  return Flusher::Flush(&ctx);
}

Status ShardEngine::MaybeGcSwap() {
  if (!gc_ || opts_.gc_reclaim_threshold_bytes == 0) return Status::Ok();
  const auto bytes = datafile_->FileSize();
  if (bytes < opts_.gc_reclaim_threshold_bytes) return Status::Ok();
  const Status st = gc_->MaybeSwap(bytes, opts_.gc_reclaim_threshold_bytes);
  if (st.ok()) stats_.gc_region_swap_total++;
  return st;
}

Status ShardEngine::CorruptSuperBlockForTest() {
  return superblock_->CorruptEpochForTest();
}

Status ShardEngine::CorruptSuperBlockSlotForTest(int slot) {
  return superblock_->CorruptSlotForTest(slot);
}

Status ShardEngine::CorruptRootForTest() {
  lazy_root_corrupt_ = true;
  recovery_mode_ = RecoveryMode::kLazy;
  return Status::Ok();
}

Status ShardEngine::CorruptWalForTest() {
  wal_corrupt_ = true;
  if (!wal_) return Status::Internal("no wal");
  std::ofstream out(wal_->path(), std::ios::binary | std::ios::trunc);
  out.write("BADWAL", 6);
  return Status::Ok();
}

Status ShardEngine::CorruptDataFileForTest(uint64_t record_offset) {
  if (!datafile_) return Status::Internal("no datafile");
  return datafile_->CorruptRecordAtOffsetForTest(record_offset);
}

Status ShardEngine::TruncateWalForTest() {
  if (!wal_) return Status::Internal("no wal");
  return wal_->TruncateAfterAppendForTest();
}

}  // namespace ebtree
