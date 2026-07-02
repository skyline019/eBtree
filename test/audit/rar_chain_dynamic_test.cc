#include <gtest/gtest.h>

#include <chrono>
#include <thread>

#ifdef _WIN32
#include <stdlib.h>
#endif

#include "digest.h"
#include "engine_test_util.h"
#include "policy_gate.h"
#include "rar_builder.h"
#include "rar_chain.h"
#include "rar_chain_worker.h"
#include "rar_snapshot_builder.h"
#include "json_writer.h"

#include "ebtree/engine/engine.h"
#include "ebtree/engine/engine_attest.h"

using namespace ebtree;
using namespace ebtree::audit;

TEST(RarChainRoundtrip, AppendVerifySequenceAndHash) {
  const std::string chain_path =
      test::TempDir("rar_chain_roundtrip") + "/chain.jsonl";
  std::filesystem::remove(chain_path);

  AttestExportReportV2 kernel{};
  kernel.checkpoint_lsn = 42;
  kernel.base.recovery.stable_lsn = 42;
  const std::string body =
      BuildChainBodyJson(1, 42, "", "ophash", 1000, kernel);
  RarChainEntry entry{};
  entry.sequence = 1;
  entry.checkpoint_lsn = 42;
  entry.prev_rar_sha256 = "";
  entry.op_log_head_sha256 = "ophash";
  entry.generated_at_unix = 1000;
  entry.body_json = body;
  entry.rar_sha256 = Sha256HexString(body);
  ASSERT_TRUE(AppendRarChainEntry(chain_path, entry).ok());

  const std::string body2 =
      BuildChainBodyJson(2, 43, entry.rar_sha256, "ophash2", 1001, kernel);
  RarChainEntry entry2{};
  entry2.sequence = 2;
  entry2.checkpoint_lsn = 43;
  entry2.prev_rar_sha256 = entry.rar_sha256;
  entry2.op_log_head_sha256 = "ophash2";
  entry2.generated_at_unix = 1001;
  entry2.body_json = body2;
  entry2.rar_sha256 = Sha256HexString(body2);
  ASSERT_TRUE(AppendRarChainEntry(chain_path, entry2).ok());

  RarChainVerifyReport report{};
  ASSERT_TRUE(VerifyRarChain(chain_path, &report).ok());
  EXPECT_TRUE(report.consistent);
  EXPECT_EQ(report.entry_count, 2u);
  EXPECT_EQ(report.last_sequence, 2u);
  EXPECT_EQ(report.last_rar_sha256, entry2.rar_sha256);
}

TEST(RarAsyncCheckpoint, WorkerAppendsAfterCheckpoint) {
  const std::string dir = test::TempDir("rar_async_cp");
  const std::string chain_path = dir + "/ebtree.rar.chain.jsonl";

  EngineOptions opts = EngineOptions::ProductionDefaults(dir);
  std::unique_ptr<Engine> engine;
  ASSERT_TRUE(Engine::Open(opts, &engine).ok());
  ASSERT_TRUE(engine->Put("async_k", "async_v").ok());

  RarChainWorker worker;
  RarChainWorkerOptions worker_opts{};
  worker_opts.chain_path = chain_path;
  worker.Start(worker_opts);
  InstallRarChainWorker(engine.get(), &worker);

  ASSERT_TRUE(engine->Checkpoint().ok());

  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(1);
  bool found = false;
  while (std::chrono::steady_clock::now() < deadline) {
    std::vector<RarChainEntry> entries;
    if (ReadRarChainEntries(chain_path, &entries).ok() && !entries.empty()) {
      found = true;
      break;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }
  worker.Stop();
  ASSERT_TRUE(found);

  RarChainVerifyReport report{};
  ASSERT_TRUE(VerifyRarChain(chain_path, &report).ok());
  EXPECT_TRUE(report.consistent);
  EXPECT_GE(report.entry_count, 1u);
}

TEST(RarChainPolicyDecompress, EvaluateSnapshotPolicyRefuses) {
  AttestExportReportV2 snapshot{};
  snapshot.compress.decompress_fail = 1;
  RarPolicy policy{};
  policy.max_decompress_fail = 0;
  std::string reason;
  EXPECT_EQ(EvaluateSnapshotPolicy(snapshot, policy, &reason),
            RarVerdict::kRefuseStart);
  EXPECT_FALSE(reason.empty());
}

TEST(RarBuildRarV3Sections, JsonContainsKernelTierContract) {
  const std::string dir = test::TempDir("rar_v3_sections");
  EngineOptions opts = EngineOptions::ProductionDefaults(dir);
  std::unique_ptr<Engine> engine;
  ASSERT_TRUE(Engine::Open(opts, &engine).ok());
  ASSERT_TRUE(engine->Put("v3k", "v3v").ok());
  ASSERT_TRUE(engine->Checkpoint().ok());

  BuildRarOptions rar_opts{};
  rar_opts.engine_path = dir;
  rar_opts.probe_keys = {"v3k"};
  RarReport report{};
  ASSERT_TRUE(BuildRar(rar_opts, &report).ok());
  const std::string json = RarReportToJson(report);
  EXPECT_NE(json.find("\"kernel\""), std::string::npos);
  EXPECT_NE(json.find("\"tier_contract\""), std::string::npos);
  EXPECT_GE(report.kernel.checkpoint_lsn, 1u);
  EXPECT_TRUE(report.tier_contract.consistent);
}

#ifndef NDEBUG
TEST(RarChainPerf, CheckpointObserverEnqueueBudget) {
  GTEST_SKIP() << "perf gate runs in Release only";
}
#else
TEST(RarChainPerf, CheckpointObserverEnqueueBudget) {
  const std::string dir = test::TempDir("rar_cp_observer_perf");
  EngineOptions opts = EngineOptions::ProductionDefaults(dir);
  std::unique_ptr<Engine> engine;
  ASSERT_TRUE(Engine::Open(opts, &engine).ok());
  ASSERT_TRUE(engine->Put("pk", "pv").ok());

  const auto run = [&](bool with_observer) -> int64_t {
    engine->SetCheckpointObserver(with_observer ? CheckpointObserver([](Engine*, uint64_t) {})
                                            : CheckpointObserver{});
    const auto start = std::chrono::steady_clock::now();
    for (int i = 0; i < 1000; ++i) {
      if (!engine->Checkpoint().ok()) return -1;
    }
    return std::chrono::duration_cast<std::chrono::microseconds>(
               std::chrono::steady_clock::now() - start)
        .count();
  };

  const int64_t baseline = run(false);
  const int64_t observed = run(true);
  ASSERT_GT(baseline, 0);
  ASSERT_GT(observed, 0);
  EXPECT_GE(static_cast<double>(baseline) / static_cast<double>(observed), 0.99);
}
#endif

#if defined(EBTREE_RAR_SIGNING)
TEST(RarChainAutoSign, WorkerSignsWhenKeySet) {
  const std::string dir = test::TempDir("rar_chain_autosign");
  const std::string chain_path = dir + "/chain.jsonl";
  const std::string key_material(32, '\x01');

#ifdef _WIN32
  _putenv_s("EBTREE_RAR_KEY", key_material.c_str());
#else
  setenv("EBTREE_RAR_KEY", key_material.c_str(), 1);
#endif

  EngineOptions opts = EngineOptions::ProductionDefaults(dir);
  std::unique_ptr<Engine> engine;
  ASSERT_TRUE(Engine::Open(opts, &engine).ok());
  ASSERT_TRUE(engine->Put("sign_k", "sign_v").ok());

  RarChainWorker worker;
  RarChainWorkerOptions worker_opts{};
  worker_opts.chain_path = chain_path;
  worker.Start(worker_opts);
  InstallRarChainWorker(engine.get(), &worker);
  ASSERT_TRUE(engine->Checkpoint().ok());

  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
  bool signed_entry = false;
  while (std::chrono::steady_clock::now() < deadline) {
    std::vector<RarChainEntry> entries;
    if (ReadRarChainEntries(chain_path, &entries).ok() && !entries.empty()) {
      if (!entries.back().signature.empty()) {
        signed_entry = true;
        break;
      }
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }
  worker.Stop();
  ASSERT_TRUE(signed_entry);

  RarChainVerifyReport report{};
  ASSERT_TRUE(VerifyRarChain(chain_path, &report).ok());
  EXPECT_TRUE(report.consistent);
}
#endif
