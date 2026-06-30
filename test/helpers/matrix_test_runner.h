#pragma once

#include <gtest/gtest.h>

#include <filesystem>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "engine_test_util.h"
#include "matrix_case.h"
#include "ebtree/concept/tlog/tlog.h"

namespace ebtree {
namespace test {

inline DurabilityClass ParseDurability(const std::string& d) {
  if (d == "group") return DurabilityClass::kGroup;
  if (d == "async") return DurabilityClass::kAsync;
  if (d == "balanced") return DurabilityClass::kBalanced;
  return DurabilityClass::kSync;
}

inline void ApplyMatrixOp(Engine* engine, const std::string& op) {
  if (op.rfind("put ", 0) == 0) {
    const auto eq = op.find('=');
    ASSERT_NE(eq, std::string::npos);
    const std::string key = op.substr(4, eq - 4);
    const std::string value = op.substr(eq + 1);
    ASSERT_TRUE(engine->Put(key, value).ok());
  } else if (op.rfind("corrupt ", 0) == 0) {
    ASSERT_TRUE(engine->CorruptSuperBlockForTest().ok());
  } else if (op == "group_commit") {
    ASSERT_TRUE(engine->GroupCommit().ok());
  } else if (op == "corrupt_wal") {
    ASSERT_TRUE(engine->CorruptWalForTest().ok());
  } else if (op == "corrupt_root") {
    ASSERT_TRUE(engine->CorruptRootForTest().ok());
  }
}

inline void AssertMatrixStat(Engine* engine, const std::string& spec) {
  if (spec.empty()) return;
  const auto eq = spec.find('=');
  ASSERT_NE(eq, std::string::npos) << spec;
  const std::string name = spec.substr(0, eq);
  const uint64_t expected = std::stoull(spec.substr(eq + 1));
  const EngineStats& stats = engine->stats();
  if (name == "fallback_read_total") {
    EXPECT_EQ(stats.fallback_read_total, expected);
  } else if (name == "unexpected_path_total") {
    EXPECT_EQ(stats.unexpected_path_total, expected);
  } else if (name == "fsync_merge_ratio") {
    EXPECT_GE(stats.fsync_merge_ratio, expected);
  } else if (name == "wal_full_scan_total") {
    EXPECT_EQ(stats.wal_full_scan_total, expected);
  } else if (name == "pages_touched") {
    EXPECT_GE(engine->btree()->pages_touched(), expected);
  } else if (name == "stable_lsn") {
    EXPECT_EQ(stats.stable_lsn, expected);
  } else if (name == "group_commit_total") {
    EXPECT_GE(stats.group_commit_total, expected);
  } else if (name == "flusher_flush_total") {
    EXPECT_GE(stats.flusher_flush_total, expected);
  } else if (name == "wal_append_total") {
    EXPECT_GE(stats.wal_append_total, expected);
  } else if (name == "tlog_snapshot_total") {
    EXPECT_EQ(stats.tlog_snapshot_total, expected);
  } else {
    FAIL() << "unknown stat: " << name;
  }
}

inline uint32_t ParseAsofTs(const EbMatrixCase& c, uint32_t default_ts = 7000u) {
  if (!c.corrupt.empty()) {
    return static_cast<uint32_t>(std::stoul(c.corrupt));
  }
  return default_ts;
}

inline void RunFlashbackGetAsOf(Engine* engine, const EbMatrixCase& c,
                                uint32_t asof_ts) {
  ASSERT_NE(engine, nullptr);
  if (c.get_key.empty()) return;
  std::string value;
  const Status st = engine->GetAsOf(c.get_key, asof_ts, &value);
  if (c.expect == "not_found") {
    EXPECT_TRUE(IsNotFound(st)) << c.id;
    return;
  }
  ASSERT_TRUE(st.ok()) << c.id;
  EXPECT_EQ(value, c.get_value);
}

inline void RunMatrixCase(const EbMatrixCase& c) {
  const std::string dir = TempDir("matrix_" + c.id);
  const auto durability = ParseDurability(c.durability);

  const auto make_engine = [&]() {
    return OpenEngine(dir, durability, c.compress_pages);
  };

  if (c.run == "flashback_scan_two_checkpoint") {
    const uint32_t ts0 = ParseAsofTs(c, 7200u);
    const uint32_t ts1 = ts0 + 100u;
    ebtree::SetTimestampSourceForTest([ts0, ts1]() {
      static int n = 0;
      if (n == 0) {
        ++n;
        return ts0;
      }
      return ts1;
    });
    {
      auto engine = make_engine();
      ASSERT_NE(engine, nullptr);
      for (const auto& op : c.setup_ops) ApplyMatrixOp(engine.get(), op);
      ASSERT_TRUE(engine->Checkpoint().ok());
      ASSERT_TRUE(engine->Put("fsv1", "a2").ok());
      ASSERT_TRUE(engine->Checkpoint().ok());
    }
    auto engine = make_engine();
    ASSERT_NE(engine, nullptr);
    TypedPlan plan;
    plan.op = PredicateOp::kRange;
    plan.key = "fsv1";
    plan.range_end = "fsv3";
    std::vector<std::pair<std::string, std::string>> rows;
    ASSERT_TRUE(engine->ScanAsOf(plan, ts0, &rows).ok()) << c.id;
    ASSERT_EQ(rows.size(), 2u) << c.id;
    EXPECT_EQ(rows[0].second, "a1");
    EXPECT_EQ(rows[1].second, "b1");
    RunFlashbackGetAsOf(engine.get(), c, ts0);
    AssertMatrixStat(engine.get(), c.assert_stat);
    ebtree::ResetTimestampSourceForTest();
    return;
  }

  if (c.run == "flashback_multi_checkpoint") {
    ebtree::SetTimestampSourceForTest([]() {
      static uint32_t ts = 7400u;
      return ts++;
    });
    auto engine = make_engine();
    ASSERT_NE(engine, nullptr);
    for (const auto& op : c.setup_ops) ApplyMatrixOp(engine.get(), op);
    ASSERT_TRUE(engine->Checkpoint().ok());
    ASSERT_TRUE(engine->Put("ft_k", "ft_v2").ok());
    ASSERT_TRUE(engine->Checkpoint().ok());
    AssertMatrixStat(engine.get(), c.assert_stat);
    ebtree::ResetTimestampSourceForTest();
    return;
  }

  if (c.run == "reopen" || c.run == "checkpoint_reopen" ||
      c.run == "flush_reopen" || c.run == "group_commit_reopen" ||
      c.run == "checkpoint_corrupt_wal_reopen" || c.run == "fast_open_reopen" ||
      c.run == "lazy_root_reopen" || c.run == "full_replay_reopen" ||
      c.run == "flashback_checkpoint" || c.run == "flashback_corrupt_wal_reopen" ||
      c.run == "memtable_3stage_reopen") {
    {
      auto engine = make_engine();
      ASSERT_NE(engine, nullptr);
      for (const auto& op : c.setup_ops) ApplyMatrixOp(engine.get(), op);
      if (c.run == "checkpoint_reopen" || c.run == "checkpoint_corrupt_wal_reopen") {
        ASSERT_TRUE(engine->Checkpoint().ok());
      } else if (c.run == "flush_reopen") {
        ASSERT_TRUE(engine->Flush().ok());
      } else if (c.run == "group_commit_reopen") {
        ASSERT_TRUE(engine->GroupCommit().ok());
      }
      if (c.run == "checkpoint_corrupt_wal_reopen") {
        ASSERT_TRUE(engine->CorruptWalForTest().ok());
      } else if (c.run == "lazy_root_reopen") {
        ASSERT_TRUE(engine->Checkpoint().ok());
        ASSERT_TRUE(engine->Put("lr2", "lv2").ok());
        ASSERT_TRUE(engine->CorruptRootForTest().ok());
        ASSERT_TRUE(engine->CommitSuperBlockInternal().ok());
      } else if (c.run == "flashback_checkpoint") {
        const uint32_t snap_ts =
            c.expect == "not_found" ? ParseAsofTs(c, 6999u) + 1u : ParseAsofTs(c);
        ebtree::SetTimestampSourceForTest([snap_ts]() { return snap_ts; });
        ASSERT_TRUE(engine->Checkpoint().ok());
      } else if (c.run == "flashback_corrupt_wal_reopen") {
        const uint32_t snap_ts = ParseAsofTs(c, 7100u);
        ebtree::SetTimestampSourceForTest([snap_ts]() { return snap_ts; });
        ASSERT_TRUE(engine->Checkpoint().ok());
        ASSERT_TRUE(engine->CorruptWalForTest().ok());
      } else if (c.run == "memtable_3stage_reopen") {
        engine->RotateMemTableForFlush();
        ASSERT_TRUE(engine->Flush().ok());
      }
      if (!c.assert_stat.empty() && c.run != "checkpoint_reopen") {
        AssertMatrixStat(engine.get(), c.assert_stat);
      }
    }
    auto engine = make_engine();
    if (c.run == "full_replay_reopen") {
      ebtree::EngineOptions opts;
      opts.path = dir;
      opts.durability = durability;
      opts.compress_pages = c.compress_pages;
      opts.recovery_strategy = ebtree::RecoveryStrategy::kFullReplay;
      std::unique_ptr<ebtree::Engine> full;
      ASSERT_TRUE(ebtree::Engine::Open(opts, &full).ok());
      ASSERT_NE(full, nullptr);
      EXPECT_FALSE(full->wal_replay_pending());
      if (c.expect == "fail") {
        return;
      }
      if (!c.get_key.empty()) {
        std::string value;
        const Status st = full->Get(c.get_key, &value);
        ASSERT_TRUE(st.ok()) << c.id;
        EXPECT_EQ(value, c.get_value);
      }
      ebtree::ResetTimestampSourceForTest();
      return;
    }
    if (c.run == "flashback_checkpoint" || c.run == "flashback_corrupt_wal_reopen") {
      ebtree::EngineOptions opts;
      opts.path = dir;
      opts.durability = durability;
      opts.compress_pages = c.compress_pages;
      std::unique_ptr<ebtree::Engine> fb;
      ASSERT_TRUE(ebtree::Engine::Open(opts, &fb).ok());
      ASSERT_NE(fb, nullptr);
      const uint32_t asof_ts = ParseAsofTs(c);
      RunFlashbackGetAsOf(fb.get(), c, asof_ts);
      if (!c.assert_stat.empty()) {
        AssertMatrixStat(fb.get(), c.assert_stat);
      }
      ebtree::ResetTimestampSourceForTest();
      return;
    }
    if (c.run == "fast_open_reopen") {
      ebtree::EngineOptions opts;
      opts.path = dir;
      opts.durability = durability;
      opts.compress_pages = c.compress_pages;
      opts.recovery_strategy = ebtree::RecoveryStrategy::kFastOpen;
      std::unique_ptr<ebtree::Engine> lazy;
      ASSERT_TRUE(ebtree::Engine::Open(opts, &lazy).ok());
      ASSERT_NE(lazy, nullptr);
      EXPECT_TRUE(lazy->wal_replay_pending());
      if (!c.get_key.empty()) {
        std::string value;
        const Status st = lazy->Get(c.get_key, &value);
        ASSERT_TRUE(st.ok()) << c.id;
        EXPECT_EQ(value, c.get_value);
      }
      AssertMatrixStat(lazy.get(), c.assert_stat);
      return;
    }
    if (c.expect == "fail") {
      ASSERT_EQ(engine, nullptr);
      return;
    }
    ASSERT_NE(engine, nullptr);
    if (!c.get_key.empty()) {
      std::string value;
      const Status st = engine->Get(c.get_key, &value);
      ASSERT_TRUE(st.ok()) << c.id;
      EXPECT_EQ(value, c.get_value);
    }
    if (!c.assert_stat.empty() && c.run == "checkpoint_reopen") {
      AssertMatrixStat(engine.get(), c.assert_stat);
    }
    return;
  }

  if (c.run == "get missing") {
    auto engine = make_engine();
    ASSERT_NE(engine, nullptr);
    std::string value;
    const Status st = engine->Get(c.get_key, &value);
    EXPECT_TRUE(IsNotFound(st));
    return;
  }

  if (c.run == "checkpoint_scan" || c.run == "scan_missing") {
    auto engine = make_engine();
    ASSERT_NE(engine, nullptr);
    for (const auto& op : c.setup_ops) ApplyMatrixOp(engine.get(), op);
    ASSERT_TRUE(engine->Checkpoint().ok());

    TypedPlan plan;
    plan.op = PredicateOp::kEq;
    plan.key = c.get_key;
    plan.snapshot_lsn = engine->stable_lsn();

    std::vector<std::pair<std::string, std::string>> rows;
    const Status st = engine->Scan(plan, &rows);
    if (c.run == "scan_missing") {
      EXPECT_TRUE(st.ok());
      EXPECT_TRUE(rows.empty());
    } else {
      ASSERT_TRUE(st.ok());
      ASSERT_EQ(rows.size(), 1u);
      EXPECT_EQ(rows[0].first, c.get_key);
      EXPECT_EQ(rows[0].second, c.get_value);
    }
    AssertMatrixStat(engine.get(), c.assert_stat);
    return;
  }

  if (c.run == "async_stable_check") {
    auto engine = make_engine();
    ASSERT_NE(engine, nullptr);
    for (const auto& op : c.setup_ops) ApplyMatrixOp(engine.get(), op);
    EXPECT_EQ(engine->stable_lsn(), 0u);
    ASSERT_TRUE(engine->Flush().ok());
    EXPECT_GT(engine->stable_lsn(), 0u);
    if (!c.get_key.empty()) {
      std::string value;
      ASSERT_TRUE(engine->Get(c.get_key, &value).ok());
      EXPECT_EQ(value, c.get_value);
    }
    AssertMatrixStat(engine.get(), c.assert_stat);
    return;
  }

  if (c.run == "balanced_concurrent_puts") {
    ebtree::EngineOptions opts = ebtree::EngineOptions::StandardDefaults(dir);
    auto engine = OpenEngineWithOptions(dir, opts);
    ASSERT_NE(engine, nullptr);
    std::vector<std::thread> pool;
    for (int t = 0; t < 4; ++t) {
      pool.emplace_back([engine = engine.get(), t]() {
        for (int i = 0; i < 8; ++i) {
          const std::string key =
              "bc" + std::to_string(t) + "_" + std::to_string(i);
          ASSERT_TRUE(engine->Put(key, "v").ok());
        }
      });
    }
    for (auto& th : pool) th.join();
    AssertMatrixStat(engine.get(), c.assert_stat);
    return;
  }

  if (c.run == "auto_batch_group_commit") {
    EngineOptions opts;
    opts.path = dir;
    opts.durability = DurabilityClass::kGroup;
    opts.group_commit_batch_size = 2;
    auto engine = OpenEngineWithOptions(dir, opts);
    ASSERT_NE(engine, nullptr);
    for (const auto& op : c.setup_ops) ApplyMatrixOp(engine.get(), op);
    AssertMatrixStat(engine.get(), c.assert_stat);
    return;
  }

  if (c.run == "scan_before_flush") {
    auto engine = make_engine();
    ASSERT_NE(engine, nullptr);
    for (const auto& op : c.setup_ops) ApplyMatrixOp(engine.get(), op);
    EXPECT_TRUE(engine->committed().empty());
    TypedPlan plan;
    plan.op = PredicateOp::kEq;
    plan.key = c.get_key;
    plan.snapshot_lsn = engine->stable_lsn();
    std::vector<std::pair<std::string, std::string>> rows;
    ASSERT_TRUE(engine->Scan(plan, &rows).ok());
    ASSERT_EQ(rows.size(), 1u);
    EXPECT_EQ(rows[0].second, c.get_value);
    return;
  }

  if (c.run == "async_reopen_lost") {
    {
      auto engine = make_engine();
      ASSERT_NE(engine, nullptr);
      for (const auto& op : c.setup_ops) ApplyMatrixOp(engine.get(), op);
      engine.reset();
    }
    std::filesystem::remove(dir + "/shard0.wal");
    auto engine = make_engine();
    ASSERT_NE(engine, nullptr);
    std::string value;
    EXPECT_TRUE(IsNotFound(engine->Get(c.get_key, &value)));
    return;
  }

  FAIL() << "unknown run: " << c.run;
}

}  // namespace test
}  // namespace ebtree
