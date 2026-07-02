#pragma once

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <memory>
#include <mutex>
#include <random>
#include <set>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#include "engine_test_util.h"
#include "ebtree/engine/engine.h"

namespace ebtree {
namespace test {

enum class ChaosOpType {
  kPut,
  kGet,
  kDelete,
  kFlush,
  kCheckpoint,
  kGroupCommit,
};

struct ChaosOp {
  ChaosOpType type{ChaosOpType::kPut};
  std::string key;
  std::string value;
};

inline bool IsDurableAtReturn(DurabilityClass tier) {
  return tier == DurabilityClass::kSync || tier == DurabilityClass::kBalanced;
}

class CommittedOracle {
 public:
  explicit CommittedOracle(DurabilityClass tier) : tier_(tier) {}

  void NoteTouched(const std::string& key) { touched_.insert(key); }

  void OnPutOk(Engine* engine, const std::string& key, const std::string& value) {
    NoteTouched(key);
    if (IsDurableAtReturn(tier_)) {
      kv_[key] = value;
      return;
    }
    pending_kv_[key] = value;
    SyncGroupCommits(engine);
  }

  void OnDeleteOk(Engine* engine, const std::string& key) {
    NoteTouched(key);
    if (IsDurableAtReturn(tier_)) {
      kv_.erase(key);
      return;
    }
    pending_kv_.erase(key);
    pending_deletes_.insert(key);
    SyncGroupCommits(engine);
  }

  void OnGroupCommitOk(Engine* engine) {
    for (const auto& entry : pending_kv_) {
      kv_[entry.first] = entry.second;
    }
    pending_kv_.clear();
    for (const auto& key : pending_deletes_) {
      kv_.erase(key);
    }
    pending_deletes_.clear();
    if (engine) {
      last_group_commit_total_ = engine->stats().group_commit_total;
    }
  }

  void SyncGroupCommits(Engine* engine) {
    if (!engine || tier_ != DurabilityClass::kGroup) return;
    if (engine->stats().group_commit_total > last_group_commit_total_) {
      OnGroupCommitOk(engine);
    }
  }

  void OnCheckpointOk() {}

  std::unordered_map<std::string, std::string> SnapshotVisible(Engine* engine) const {
    std::unordered_map<std::string, std::string> snap;
    if (!engine) return snap;
    for (const auto& key : touched_) {
      std::string value;
      if (engine->Get(key, &value).ok()) {
        snap[key] = value;
      }
    }
    return snap;
  }

  ::testing::AssertionResult VerifySnapshot(
      Engine* engine,
      const std::unordered_map<std::string, std::string>& snap) const {
    if (!engine) {
      return ::testing::AssertionFailure() << "null engine";
    }
    for (const auto& entry : snap) {
      std::string value;
      const Status st = engine->Get(entry.first, &value);
      if (!st.ok()) {
        return ::testing::AssertionFailure()
               << "missing key " << entry.first << " status=" << st.message();
      }
      if (value != entry.second) {
        return ::testing::AssertionFailure()
               << "key " << entry.first << " expected " << entry.second
               << " got " << value;
      }
    }
    for (const auto& key : touched_) {
      if (snap.count(key)) continue;
      std::string value;
      const Status st = engine->Get(key, &value);
      if (st.ok()) {
        return ::testing::AssertionFailure()
               << "unexpected key " << key << " value=" << value;
      }
    }
    return ::testing::AssertionSuccess();
  }

  ::testing::AssertionResult VerifyEngine(Engine* engine) const {
    return VerifySnapshot(engine, kv_);
  }

  ::testing::AssertionResult VerifyDurableCommitted(Engine* engine) const {
    if (!IsDurableAtReturn(tier_)) {
      return VerifyEngine(engine);
    }
    if (!engine) {
      return ::testing::AssertionFailure() << "null engine";
    }
    for (const auto& entry : kv_) {
      std::string value;
      const Status st = engine->Get(entry.first, &value);
      if (!st.ok()) {
        return ::testing::AssertionFailure()
               << "missing durable key " << entry.first << " status="
               << st.message();
      }
      if (value != entry.second) {
        return ::testing::AssertionFailure()
               << "durable key " << entry.first << " expected " << entry.second
               << " got " << value;
      }
    }
    return ::testing::AssertionSuccess();
  }

  DurabilityClass tier() const { return tier_; }
  const std::set<std::string>& touched_keys() const { return touched_; }
  const std::unordered_map<std::string, std::string>& durable_kv() const {
    return kv_;
  }
  const std::unordered_map<std::string, std::string>& pending_kv() const {
    return pending_kv_;
  }

 private:
  DurabilityClass tier_;
  uint64_t last_group_commit_total_{0};
  std::set<std::string> touched_;
  std::unordered_map<std::string, std::string> kv_;
  std::unordered_map<std::string, std::string> pending_kv_;
  std::set<std::string> pending_deletes_;
};

inline std::vector<ChaosOp> GeneratePowerfailOps(uint32_t seed, size_t count,
                                                 DurabilityClass tier,
                                                 bool with_control_ops,
                                                 bool large_payload = false) {
  std::mt19937 rng(seed);
  std::uniform_int_distribution<int> key_dist(0, 99);
  std::vector<ChaosOp> ops;
  ops.reserve(count);
  for (size_t i = 0; i < count; ++i) {
    ChaosOp op;
    if (tier == DurabilityClass::kGroup) {
      std::uniform_int_distribution<int> group_op_dist(0, 3);
      const int kind = group_op_dist(rng);
      op.type = kind == 3 ? ChaosOpType::kGroupCommit
                          : static_cast<ChaosOpType>(kind);
    } else if (with_control_ops) {
      std::uniform_int_distribution<int> ctrl_dist(0, 4);
      const int kind = ctrl_dist(rng);
      if (kind >= 3) {
        op.type = kind == 3 ? ChaosOpType::kFlush : ChaosOpType::kCheckpoint;
      } else {
        op.type = static_cast<ChaosOpType>(kind);
      }
    } else {
      std::uniform_int_distribution<int> data_op_dist(0, 2);
      op.type = static_cast<ChaosOpType>(data_op_dist(rng));
    }
    op.key = "pf" + std::to_string(key_dist(rng));
    if (large_payload && op.type == ChaosOpType::kPut) {
      op.value.assign(320, 'x');
      op.value[0] = static_cast<char>('a' + static_cast<int>(i % 26));
    } else {
      op.value = "v" + std::to_string(static_cast<int>(i));
    }
    ops.push_back(op);
  }
  return ops;
}

inline Status ExecuteChaosOp(Engine* engine, const ChaosOp& op,
                             CommittedOracle* oracle) {
  if (!engine) return Status::InvalidArgument("null engine");
  switch (op.type) {
    case ChaosOpType::kPut: {
      const Status st = engine->Put(op.key, op.value);
      if (st.ok() && oracle) oracle->OnPutOk(engine, op.key, op.value);
      return st;
    }
    case ChaosOpType::kDelete: {
      const Status st = engine->Delete(op.key);
      if (st.ok() && oracle) oracle->OnDeleteOk(engine, op.key);
      return st;
    }
    case ChaosOpType::kGet: {
      std::string value;
      const Status st = engine->Get(op.key, &value);
      if (st.code() == StatusCode::kNotFound) return Status::Ok();
      return st;
    }
    case ChaosOpType::kFlush: {
      return engine->Flush();
    }
    case ChaosOpType::kCheckpoint: {
      const Status st = engine->Checkpoint();
      if (st.ok() && oracle) oracle->OnCheckpointOk();
      return st;
    }
    case ChaosOpType::kGroupCommit: {
      const Status st = engine->GroupCommit();
      if (st.ok() && oracle) oracle->OnGroupCommitOk(engine);
      return st;
    }
  }
  return Status::InvalidArgument("unknown op");
}

struct PowerfailFuzzResult {
  uint32_t seed{0};
  size_t destroy_index{0};
  size_t ops_executed{0};
};

inline PowerfailFuzzResult RunRandomPowerfailOnce(const EngineOptions& opts_in,
                                                  uint32_t seed, size_t op_count,
                                                  size_t destroy_index,
                                                  bool with_control_ops = false,
                                                  bool require_no_fallback = false,
                                                  bool large_payload = false) {
  PowerfailFuzzResult result;
  result.seed = seed;
  result.destroy_index = destroy_index;

  EngineOptions opts = opts_in;
  const std::string dir = opts.path.empty() ? TempDir("pf_fuzz") : opts.path;
  opts.path = dir;

  const auto ops = GeneratePowerfailOps(seed, op_count, opts.durability,
                                        with_control_ops, large_payload);
  CommittedOracle oracle(opts.durability);
  const size_t limit = std::min(destroy_index, ops.size());
  std::unordered_map<std::string, std::string> pre_destroy;

  {
    std::unique_ptr<Engine> engine;
    if (!Engine::Open(opts, &engine).ok()) {
      return result;
    }
    for (size_t i = 0; i < limit; ++i) {
      if (!ExecuteChaosOp(engine.get(), ops[i], &oracle).ok()) {
        break;
      }
      result.ops_executed = i + 1;
    }
    if (large_payload) {
      (void)engine->Flush();
    }
    pre_destroy = oracle.SnapshotVisible(engine.get());
  }

  std::unique_ptr<Engine> reopened;
  if (!Engine::Open(opts, &reopened).ok()) {
    return result;
  }
  const auto verify =
      with_control_ops ? oracle.VerifyDurableCommitted(reopened.get())
                       : oracle.VerifySnapshot(reopened.get(), pre_destroy);
  EXPECT_TRUE(verify) << verify.message() << " seed=" << seed
                      << " destroy_index=" << destroy_index;
  if (require_no_fallback) {
    EXPECT_EQ(reopened->stats().unexpected_path_total, 0u)
        << " seed=" << seed;
  }
  return result;
}

inline void RunRandomPowerfailFuzz(const EngineOptions& opts_in,
                                   uint32_t base_seed, int trials,
                                   size_t op_count,
                                   bool require_no_fallback = false,
                                   bool large_payload = false) {
  std::mt19937 rng(base_seed);
  std::uniform_int_distribution<size_t> destroy_dist(1, op_count);
  for (int t = 0; t < trials; ++t) {
    const uint32_t seed = static_cast<uint32_t>(base_seed + t);
    const size_t destroy_index = destroy_dist(rng);
    EngineOptions opts = opts_in;
    if (opts.path.empty()) {
      opts.path = TempDir("pf_fuzz_" + std::to_string(seed));
    } else {
      opts.path = opts.path + "/trial_" + std::to_string(t);
    }
    RunRandomPowerfailOnce(opts, seed, op_count, destroy_index, false,
                           require_no_fallback, large_payload);
  }
}

inline void RunConcurrentRandomPowerfail(const EngineOptions& opts_in,
                                         uint32_t seed, int writer_threads,
                                         int ops_per_thread,
                                         int destroy_delay_ms) {
  EngineOptions opts = opts_in;
  opts.path = TempDir("pf_conc_" + std::to_string(seed));
  std::unique_ptr<Engine> engine;
  ASSERT_TRUE(Engine::Open(opts, &engine).ok());

  CommittedOracle oracle(opts.durability);
  std::mutex oracle_mu;
  std::atomic<bool> stop{false};
  std::vector<std::thread> pool;
  pool.reserve(static_cast<size_t>(writer_threads));

  for (int t = 0; t < writer_threads; ++t) {
    pool.emplace_back([&, t]() {
      for (int i = 0; i < ops_per_thread && !stop.load(); ++i) {
        const std::string key = "cw" + std::to_string(t) + "_" + std::to_string(i);
        const std::string value = "v" + std::to_string(i);
        if (!engine->Put(key, value).ok()) continue;
        std::lock_guard<std::mutex> lock(oracle_mu);
        oracle.OnPutOk(engine.get(), key, value);
      }
    });
  }

  std::mt19937 rng(seed);
  std::uniform_int_distribution<int> delay_dist(1, destroy_delay_ms);
  std::this_thread::sleep_for(
      std::chrono::milliseconds(delay_dist(rng)));
  stop = true;
  for (auto& th : pool) th.join();
  const auto pre_destroy = oracle.SnapshotVisible(engine.get());
  engine.reset();

  std::unique_ptr<Engine> reopened;
  ASSERT_TRUE(Engine::Open(opts, &reopened).ok());
  const auto verify = oracle.VerifySnapshot(reopened.get(), pre_destroy);
  ASSERT_TRUE(verify) << verify.message();
}

}  // namespace test
}  // namespace ebtree
