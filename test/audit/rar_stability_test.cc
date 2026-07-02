#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <thread>
#include <vector>

#include "engine_test_util.h"
#include "powerfail_fuzz.h"
#include "rar_monitor.h"

#include "ebtree/engine/engine.h"

using namespace ebtree;
using namespace ebtree::audit;

namespace {

RarMonitorOptions StabilityMonitorOptions(const std::string& dir) {
  RarMonitorOptions opts{};
  opts.enabled = true;
  opts.chain_path = dir + "/ebtree.rar.chain.jsonl";
  opts.write_circuit = true;
  opts.runtime_policy.require_unexpected_path_zero = true;
  return opts;
}

}  // namespace

TEST(RarStability, WriteGuardSurvivesPowerfailReopen) {
  const std::string dir = test::TempDir("rar_stab_pf_reopen");
  EngineOptions engine_opts = EngineOptions::ProductionDefaults(dir);
  RarMonitorOptions rar_opts = StabilityMonitorOptions(dir);

  test::RunRandomPowerfailOnce(engine_opts, 8801, 80, 40, false, true, false);

  std::unique_ptr<Engine> engine;
  RarMonitor monitor;
  ASSERT_TRUE(OpenWithRarMonitor(engine_opts, rar_opts, &engine, &monitor).ok());
  ASSERT_TRUE(engine->Put("reopen_k", "reopen_v").ok());

  if (EngineStats* stats = engine->mutable_stats()) {
    ++stats->unexpected_path_total;
  }

  const Status blocked = engine->Put("reopen_k2", "blocked");
  EXPECT_FALSE(blocked.ok());
  EXPECT_EQ(blocked.code(), StatusCode::kCorrupt);
}

TEST(RarStability, RealChainQueueDropOpensCircuit) {
  const std::string dir = test::TempDir("rar_stab_real_drop");
  EngineOptions engine_opts = EngineOptions::ProductionDefaults(dir);
  RarMonitorOptions rar_opts = StabilityMonitorOptions(dir);
  rar_opts.reject_on_chain_drop = true;
  rar_opts.max_queue_depth = 0;

  std::unique_ptr<Engine> engine;
  RarMonitor monitor;
  ASSERT_TRUE(OpenWithRarMonitor(engine_opts, rar_opts, &engine, &monitor).ok());
  ASSERT_TRUE(engine->Put("drop_k", "drop_v").ok());

  ASSERT_TRUE(engine->Checkpoint().ok());
  monitor.RefreshRuntimeState();

  EXPECT_GT(engine->stats().rar_chain_drop_total, 0u);
  EXPECT_FALSE(monitor.AllowsWrite());

  const Status blocked = engine->Put("drop_k2", "blocked");
  EXPECT_FALSE(blocked.ok());
  EXPECT_EQ(blocked.code(), StatusCode::kCorrupt);
}

TEST(RarStability, ConcurrentWriteCircuitRace) {
  const std::string dir = test::TempDir("rar_stab_concurrent");
  EngineOptions engine_opts = EngineOptions::ProductionDefaults(dir);
  RarMonitorOptions rar_opts = StabilityMonitorOptions(dir);

  std::unique_ptr<Engine> engine;
  RarMonitor monitor;
  ASSERT_TRUE(OpenWithRarMonitor(engine_opts, rar_opts, &engine, &monitor).ok());
  ASSERT_TRUE(engine->Put("race_seed", "v").ok());

  std::atomic<bool> circuit_open{false};
  std::atomic<int> blocked_puts{0};
  std::atomic<int> ok_puts{0};

  auto writer = [&]() {
    for (int i = 0; i < 64; ++i) {
      const Status st = engine->Put("race_k" + std::to_string(i), "v");
      if (st.ok()) {
        ++ok_puts;
      } else if (st.code() == StatusCode::kCorrupt) {
        ++blocked_puts;
      }
    }
  };

  std::thread t1(writer);
  std::thread t2(writer);
  std::thread t3([&]() {
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
    if (EngineStats* stats = engine->mutable_stats()) {
      ++stats->unexpected_path_total;
    }
    monitor.RefreshRuntimeState();
    circuit_open.store(true);
  });

  t1.join();
  t2.join();
  t3.join();

  EXPECT_TRUE(circuit_open.load());
  EXPECT_GT(blocked_puts.load(), 0);
  EXPECT_FALSE(monitor.AllowsWrite());
}
