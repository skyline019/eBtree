#include "rar_chain_worker.h"

#include "digest.h"
#include "op_log_head_hash.h"
#include "policy_gate.h"
#include "rar_chain.h"
#include "rar_chain_anchor.h"
#include "rar_chain_rotate.h"
#include "rar_merkle.h"
#include "rar_sign.h"
#include "rar_snapshot_builder.h"

#include "ebtree/engine/engine_attest.h"

#include <chrono>
#include <cstdlib>
#include <iostream>

namespace ebtree {
namespace audit {

namespace {

void WarnMissingSigningKeyOnce() {
  static std::atomic<bool> warned{false};
  if (warned.exchange(true)) return;
  const char* key = std::getenv("EBTREE_RAR_KEY");
  if (key && key[0] != '\0') {
    std::cerr << "rar chain: EBTREE_RAR_KEY set but signing disabled at build "
                 "(enable EBTREE_RAR_SIGNING)\n";
  } else {
    std::cerr << "rar chain: EBTREE_RAR_KEY not set; chain entries unsigned\n";
  }
}

bool RarLogEnabled() {
  const char* log = std::getenv("EBTREE_RAR_LOG");
  return log && (std::string(log) == "warn" || std::string(log) == "1");
}

}  // namespace

void RarChainWorker::Start(const RarChainWorkerOptions& opts) {
  Stop();
  if (!opts.enabled) return;
  opts_ = opts;
  stop_requested_.store(false);
  running_.store(true);
  WarnMissingSigningKeyOnce();
  worker_ = std::thread([this]() { WorkerLoop(); });
}

void RarChainWorker::Stop() {
  if (!running_.load()) return;
  stop_requested_.store(true);
  cv_.notify_all();
  if (worker_.joinable()) worker_.join();
  running_.store(false);
  {
    std::lock_guard<std::mutex> lock(mu_);
    queue_.clear();
  }
}

size_t RarChainWorker::pending_jobs() const {
  std::lock_guard<std::mutex> lock(mu_);
  return queue_.size();
}

void RarChainWorker::Enqueue(Engine* engine, uint64_t checkpoint_lsn) {
  if (!running_.load() || !engine) return;
  Job job{engine, checkpoint_lsn};
  {
    std::lock_guard<std::mutex> lock(mu_);
    if (queue_.size() >= opts_.max_queue_depth) {
      if (EngineStats* stats = engine->mutable_stats()) {
        ++stats->rar_chain_drop_total;
      }
      if (RarLogEnabled()) {
        std::cerr << "rar chain: queue full; dropped checkpoint job\n";
      }
      return;
    }
    queue_.push_back(job);
  }
  cv_.notify_one();
}

void RarChainWorker::WorkerLoop() {
  while (!stop_requested_.load()) {
    Job job{};
    {
      std::unique_lock<std::mutex> lock(mu_);
      cv_.wait(lock, [this]() {
        return stop_requested_.load() || !queue_.empty();
      });
      if (stop_requested_.load() && queue_.empty()) break;
      if (queue_.empty()) continue;
      job = queue_.front();
      queue_.pop_front();
    }
    (void)ProcessJob(job);
  }
}

void RarChainWorker::MaybeSignEntry(RarChainEntry* entry) {
  if (!entry) return;
  const char* key = std::getenv("EBTREE_RAR_KEY");
  if (!key || key[0] == '\0') return;
#ifdef EBTREE_RAR_SIGNING
  std::string sig;
  if (SignRarJson(entry->body_json, key, &sig).ok()) {
    entry->signature = sig;
  }
#else
  (void)key;
#endif
}

Status RarChainWorker::ProcessJob(const Job& job) {
  if (!job.engine) return Status::InvalidArgument("null engine");

  RarChainEntry tail{};
  bool has_tail = false;
  (void)ReadLastRarChainEntry(opts_.chain_path, &tail, &has_tail);
  if (has_tail && tail.sequence >= opts_.rotate_max_entries) {
    MaybeAutoPublishCarlAnchor(opts_.chain_path);
    (void)RotateRarChainIfNeeded(opts_.chain_path, opts_.rotate_max_entries);
    has_tail = false;
  }

  AttestExportReportV2 snapshot{};
  const Status es =
      AttestExportSnapshot(job.engine, job.checkpoint_lsn, &snapshot);
  if (!es.ok()) return es;

  std::string reason;
  const RarVerdict verdict =
      EvaluateSnapshotPolicy(snapshot, opts_.runtime_policy, &reason);

  const uint64_t sequence = has_tail ? tail.sequence + 1 : 1;
  const std::string prev = has_tail ? tail.rar_sha256 : "";
  const int64_t now = std::chrono::duration_cast<std::chrono::seconds>(
                          std::chrono::system_clock::now().time_since_epoch())
                          .count();

  std::string op_log_hash;
  if (!opts_.op_log_path.empty()) {
    (void)OpLogHeadSha256(opts_.op_log_path, &op_log_hash);
  }

  const std::string body = BuildChainBodyJson(
      sequence, snapshot.checkpoint_lsn, prev, op_log_hash, now, snapshot);

  RarChainEntry entry{};
  entry.sequence = sequence;
  entry.checkpoint_lsn = snapshot.checkpoint_lsn;
  entry.prev_rar_sha256 = prev;
  entry.op_log_head_sha256 = op_log_hash;
  entry.generated_at_unix = now;
  entry.body_json = body;
  entry.kernel_json = AttestExportV2ToJson(snapshot);
  entry.rar_sha256 = Sha256HexString(body);
  MaybeSignEntry(&entry);

  const Status as = AppendRarChainEntry(opts_.chain_path, entry);
  if (!as.ok()) return as;

  merkle_.AppendLeaf(sequence, entry.rar_sha256);
  if (merkle_.ShouldFlush()) {
    const CarlMerkleBatch batch = merkle_.FlushBatch();
    (void)PersistCarlMerkleBatch(CarlMerkleSidecarPath(opts_.chain_path), batch);
    MaybeAutoPublishCarlAnchor(opts_.chain_path);
  }

  if (opts_.on_snapshot) {
    RarChainSnapshotEvent event{};
    event.snapshot = snapshot;
    event.verdict = verdict;
    event.reason = reason;
    event.sequence = sequence;
    opts_.on_snapshot(event);
  }
  return Status::Ok();
}

void InstallRarChainWorker(Engine* engine, RarChainWorker* worker) {
  if (!engine || !worker) return;
  engine->SetCheckpointObserver([worker](Engine* eng, uint64_t checkpoint_lsn) {
    worker->Enqueue(eng, checkpoint_lsn);
  });
}

}  // namespace audit
}  // namespace ebtree
