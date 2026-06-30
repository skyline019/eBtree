#include <fstream>
#include <gtest/gtest.h>

#include "digest.h"
#include "op_log_expect.h"
#include "policy_gate.h"
#include "rar_builder.h"
#include "engine_test_util.h"

namespace ebtree {
namespace audit {
namespace {

TEST(RarPolicyWarn, PendingUncommittedWarns) {
  const std::string dir = test::TempDir("rar_policy_warn");
  const std::string op_log = dir + "/ebtree.op_log.jsonl";

  {
    std::unique_ptr<Engine> engine;
    EngineOptions opts = EngineOptions::BenchmarkGroupDefaults(dir);
    opts.path = dir;
    ASSERT_TRUE(Engine::Open(opts, &engine).ok());
    ASSERT_TRUE(engine->Put("pending_k", "pending_v").ok());
  }

  {
    std::ofstream out(op_log, std::ios::trunc);
    ASSERT_TRUE(out);
    out << "{\"v\":1,\"op\":\"put\",\"key\":\"pending_k\",\"value_sha256\":\""
        << Sha256HexString("pending_v")
        << "\",\"lsn\":1,\"durable_at_return\":false,\"tier\":\"group\","
           "\"ts_unix\":1}\n";
  }

  ExpectSnapshot expect{};
  ASSERT_TRUE(
      LoadOpLogExpectSnapshot(op_log, ContractMode::kDurable, &expect).ok());

  BuildRarOptions build{};
  build.engine_path = dir;
  build.durability_tier = DurabilityClass::kGroup;
  build.expect = &expect;
  build.op_log_path = op_log;
  build.probe_keys = CollectProbeKeysFromExpect(expect);

  RarReport report{};
  ASSERT_TRUE(BuildRar(build, &report).ok());
  EXPECT_EQ(report.verdict, RarVerdict::kWarn);
  EXPECT_FALSE(report.contract.pending_uncommitted.empty());
  EXPECT_EQ(ApplyPolicyGate(report, nullptr), RarVerdict::kWarn);
}

TEST(RarPolicyWarn, GroupCommitThenPass) {
  const std::string dir = test::TempDir("rar_policy_gc_pass");
  const std::string op_log = dir + "/ebtree.op_log.jsonl";

  {
    std::unique_ptr<Engine> engine;
    EngineOptions opts = EngineOptions::BenchmarkGroupDefaults(dir);
    opts.path = dir;
    ASSERT_TRUE(Engine::Open(opts, &engine).ok());
    ASSERT_TRUE(engine->Put("gc_k", "gc_v").ok());
    ASSERT_TRUE(engine->GroupCommit().ok());
  }

  {
    std::ofstream out(op_log, std::ios::trunc);
    ASSERT_TRUE(out);
    out << "{\"v\":1,\"op\":\"put\",\"key\":\"gc_k\",\"value_sha256\":\""
        << Sha256HexString("gc_v")
        << "\",\"lsn\":1,\"durable_at_return\":true,\"tier\":\"group\","
           "\"ts_unix\":1}\n";
  }

  ExpectSnapshot expect{};
  ASSERT_TRUE(
      LoadOpLogExpectSnapshot(op_log, ContractMode::kDurable, &expect).ok());

  BuildRarOptions build{};
  build.engine_path = dir;
  build.durability_tier = DurabilityClass::kGroup;
  build.expect = &expect;
  build.op_log_path = op_log;
  build.probe_keys = CollectProbeKeysFromExpect(expect);

  RarReport report{};
  ASSERT_TRUE(BuildRar(build, &report).ok());
  EXPECT_EQ(report.verdict, RarVerdict::kPass);
  EXPECT_TRUE(report.contract.pending_uncommitted.empty());
}

TEST(RarPolicyWarn, RecoveryMaxMissingRefuse) {
  RarReport report{};
  report.policy.recovery_max_missing = 0;
  report.contract.missing.push_back(ContractKeyIssue{});
  EXPECT_EQ(ApplyPolicyGate(report, nullptr), RarVerdict::kRefuseStart);
}

}  // namespace
}  // namespace audit
}  // namespace ebtree
