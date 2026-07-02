#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <random>
#include <thread>
#include <vector>

#include "engine_test_util.h"
#include "ebtree/engine/engine.h"
#include "snapshot_oracle.h"

namespace {

int SnapshotPowerfailTrials() {
#if defined(EBTEST_CI)
  return 2;
#else
  return 4;
#endif
}

size_t SnapshotPowerfailOps() {
#if defined(EBTEST_CI)
  return 30;
#else
  return 40;
#endif
}

void RefreshOracleFromEngine(ebtree::Engine* engine,
                             ebtree::test::SnapshotOracle* oracle) {
  if (!engine || !oracle) return;
  const auto snap = engine->CaptureSnapshot();
  const uint64_t lsn = snap.ForShard(0);
  for (int k = 0; k < 16; ++k) {
    const std::string key = "rk" + std::to_string(k);
    std::string value;
    if (engine->GetAtSnapshot(key, snap, 0, &value).ok()) {
      oracle->OnPut(lsn, key, value);
    } else {
      oracle->OnDelete(lsn, key);
    }
  }
}

void RunConcurrentSnapshotPowerfail(ebtree::DurabilityClass durability) {
  std::mt19937 rng(9001);
  const ebtree::CheckpointPhase phases[] = {
      ebtree::CheckpointPhase::AfterFlush,
      ebtree::CheckpointPhase::AfterVcsFold,
      ebtree::CheckpointPhase::AfterTLog,
      ebtree::CheckpointPhase::BeforeSuperBlock,
      ebtree::CheckpointPhase::AfterSuperBlock,
      ebtree::CheckpointPhase::BeforeWalTruncate,
  };

  for (int trial = 0; trial < SnapshotPowerfailTrials(); ++trial) {
    const std::string dir =
        ebtree::test::TempDir("vcs_snap_pf_" + std::to_string(trial));
    ebtree::EngineOptions opts =
        ebtree::test::LsvPowerfailOptions(dir, durability, true);
    std::unique_ptr<ebtree::Engine> engine;
    ASSERT_TRUE(ebtree::Engine::Open(opts, &engine).ok());
    ebtree::test::SnapshotOracle oracle;

    const int phase_idx =
        static_cast<int>(rng() % (sizeof(phases) / sizeof(phases[0])));
    engine->SetCheckpointHookForTest([phase = phases[phase_idx]](
                                         ebtree::CheckpointPhase p) {
      return p == phase;
    });

    std::atomic<bool> stop{false};
    std::atomic<uint32_t> next_txn{1};

    const int reader_count = [&]() {
#if defined(EBTEST_CI)
      if (durability != ebtree::DurabilityClass::kSync) return 1;
      return 2;
#else
      if (durability != ebtree::DurabilityClass::kSync) return 1;
      return 4;
#endif
    }();
    const auto reader_sleep = (durability == ebtree::DurabilityClass::kSync)
                                  ? std::chrono::microseconds(50)
                                  : std::chrono::milliseconds(2);

    auto reader = [&]() {
      while (!stop.load(std::memory_order_acquire)) {
        const ebtree::SnapshotToken snap = engine->CaptureSnapshot();
        const uint32_t txn_id = next_txn.fetch_add(1);
        (void)engine->PinSnapshot(snap);
        for (int i = 0; i < 4; ++i) {
          if (stop.load(std::memory_order_acquire)) break;
          const std::string key = "rk" + std::to_string(i % 16);
          std::string value;
          (void)engine->GetAtSnapshot(key, snap, txn_id, &value);
        }
        (void)engine->ReleaseSnapshot(snap);
        std::this_thread::sleep_for(reader_sleep);
      }
    };

    std::vector<std::thread> readers;
    for (int i = 0; i < reader_count; ++i) {
      readers.emplace_back(reader);
    }

    for (size_t i = 0; i < SnapshotPowerfailOps(); ++i) {
      const std::string key = "rk" + std::to_string(i % 16);
      const std::string val = "v" + std::to_string(i);
      const uint32_t wtxn = next_txn.fetch_add(1);
      if ((i % 13) == 0) {
        (void)engine->Delete(key, wtxn);
      } else {
        ASSERT_TRUE(engine->Put(key, val, wtxn).ok());
      }
      if (i > 0 && (i % 19) == 0) {
        stop.store(true, std::memory_order_release);
        for (auto& th : readers) {
          if (th.joinable()) th.join();
        }
        readers.clear();
        stop.store(false, std::memory_order_release);
        (void)engine->Checkpoint();
        engine.reset();
        ASSERT_TRUE(ebtree::Engine::Open(opts, &engine).ok());
        engine->SetCheckpointHookForTest([phase = phases[phase_idx]](
                                             ebtree::CheckpointPhase p) {
          return p == phase;
        });
        RefreshOracleFromEngine(engine.get(), &oracle);
        for (int r = 0; r < reader_count; ++r) {
          readers.emplace_back(reader);
        }
      }
    }

    stop.store(true, std::memory_order_release);
    for (auto& th : readers) {
      if (th.joinable()) th.join();
    }

    engine.reset();
    ASSERT_TRUE(ebtree::Engine::Open(opts, &engine).ok());
    RefreshOracleFromEngine(engine.get(), &oracle);
    const auto snap = engine->CaptureSnapshot();
    EXPECT_TRUE(oracle.VerifyEngineAtSnapshot(engine.get(), snap, 0));
    EXPECT_EQ(engine->stats().unexpected_path_total, 0u);
  }
}

TEST(VcsSnapshotPowerfailTest, SyncConcurrentSnapshotTxnDestroy) {
  RunConcurrentSnapshotPowerfail(ebtree::DurabilityClass::kSync);
}

TEST(VcsSnapshotPowerfailTest, BalancedConcurrentSnapshotTxnDestroy) {
  RunConcurrentSnapshotPowerfail(ebtree::DurabilityClass::kBalanced);
}

TEST(VcsSnapshotPowerfailTest, GroupConcurrentSnapshotTxnDestroy) {
  RunConcurrentSnapshotPowerfail(ebtree::DurabilityClass::kGroup);
}

}  // namespace
