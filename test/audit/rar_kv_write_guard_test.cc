#include <gtest/gtest.h>

#include "engine_test_util.h"
#include "rar_monitor.h"

#include "ebtree/engine/engine.h"

using namespace ebtree;
using namespace ebtree::audit;

TEST(KvRarMonitor, WriteCircuitBlocksPut) {
  const std::string dir = test::TempDir("kv_rar_write_guard");
  EngineOptions opts = EngineOptions::ProductionDefaults(dir);
  RarMonitorOptions rar_opts{};
  rar_opts.enabled = true;
  rar_opts.chain_path = dir + "/ebtree.rar.chain.jsonl";
  rar_opts.write_circuit = true;
  rar_opts.runtime_policy.require_unexpected_path_zero = true;

  std::unique_ptr<Engine> engine;
  RarMonitor monitor;
  ASSERT_TRUE(OpenWithRarMonitor(opts, rar_opts, &engine, &monitor).ok());
  ASSERT_TRUE(engine->Put("k0", "v0").ok());

  if (EngineStats* stats = engine->mutable_stats()) {
    ++stats->unexpected_path_total;
  }

  const Status put_st = engine->Put("k1", "v1");
  EXPECT_FALSE(put_st.ok());
  EXPECT_EQ(put_st.code(), StatusCode::kCorrupt);

  std::string value;
  EXPECT_TRUE(engine->Get("k0", &value).ok());
  EXPECT_EQ(value, "v0");
}

TEST(KvRarMonitor, WriteCircuitBlocksDelete) {
  const std::string dir = test::TempDir("kv_rar_delete_guard");
  EngineOptions opts = EngineOptions::ProductionDefaults(dir);
  RarMonitorOptions rar_opts{};
  rar_opts.enabled = true;
  rar_opts.chain_path = dir + "/ebtree.rar.chain.jsonl";
  rar_opts.write_circuit = true;
  rar_opts.runtime_policy.require_unexpected_path_zero = true;

  std::unique_ptr<Engine> engine;
  RarMonitor monitor;
  ASSERT_TRUE(OpenWithRarMonitor(opts, rar_opts, &engine, &monitor).ok());
  ASSERT_TRUE(engine->Put("dk", "dv").ok());

  if (EngineStats* stats = engine->mutable_stats()) {
    ++stats->unexpected_path_total;
  }

  EXPECT_FALSE(engine->Delete("dk").ok());
  std::string value;
  EXPECT_TRUE(engine->Get("dk", &value).ok());
}

TEST(KvRarMonitor, RejectOnChainDropOpensWriteCircuit) {
  const std::string dir = test::TempDir("kv_rar_chain_drop");
  EngineOptions opts = EngineOptions::ProductionDefaults(dir);
  RarMonitorOptions rar_opts{};
  rar_opts.enabled = true;
  rar_opts.chain_path = dir + "/ebtree.rar.chain.jsonl";
  rar_opts.write_circuit = true;
  rar_opts.reject_on_chain_drop = true;
  rar_opts.runtime_policy.require_unexpected_path_zero = true;

  std::unique_ptr<Engine> engine;
  RarMonitor monitor;
  ASSERT_TRUE(OpenWithRarMonitor(opts, rar_opts, &engine, &monitor).ok());
  ASSERT_TRUE(engine->Put("cd0", "v").ok());

  if (EngineStats* stats = engine->mutable_stats()) {
    stats->rar_chain_drop_total = engine->stats().rar_chain_drop_total + 1;
  }
  monitor.RefreshRuntimeState();
  EXPECT_FALSE(monitor.AllowsWrite());
  EXPECT_FALSE(engine->Put("cd1", "v2").ok());
}
