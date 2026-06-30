#include <fstream>
#include <gtest/gtest.h>

#include "catalog_expect.h"
#include "digest.h"
#include "op_log_expect.h"
#include "rar_builder.h"
#include "engine_test_util.h"

namespace ebtree {
namespace audit {
namespace {

TEST(RarMultishardOpLog, OpLogContractAcrossShards) {
  const std::string dir = test::TempDir("rar_multishard");
  const std::string op_log = dir + "/ebtree.op_log.jsonl";

  EngineOptions opts = EngineOptions::ProductionDefaults(dir);
  opts.path = dir;
  opts.shard_count = 4;
  opts.durability = DurabilityClass::kBalanced;

  {
    std::unique_ptr<Engine> engine;
    ASSERT_TRUE(Engine::Open(opts, &engine).ok());
    ASSERT_TRUE(engine->Put("1:shard_a", "va").ok());
    ASSERT_TRUE(engine->Put("1:shard_b", "vb").ok());
  }

  {
    std::ofstream out(op_log, std::ios::trunc);
    ASSERT_TRUE(out);
    out << "{\"v\":1,\"op\":\"put\",\"key\":\"1:shard_a\",\"value_sha256\":\""
        << Sha256HexString("va")
        << "\",\"lsn\":1,\"durable_at_return\":true,\"tier\":\"balanced\","
           "\"ts_unix\":1}\n";
    out << "{\"v\":1,\"op\":\"put\",\"key\":\"1:shard_b\",\"value_sha256\":\""
        << Sha256HexString("vb")
        << "\",\"lsn\":2,\"durable_at_return\":true,\"tier\":\"balanced\","
           "\"ts_unix\":2}\n";
  }

  ExpectSnapshot expect{};
  ASSERT_TRUE(
      LoadOpLogExpectSnapshot(op_log, ContractMode::kDurable, &expect).ok());
  EXPECT_EQ(expect.touched_keys.size(), 2u);

  BuildRarOptions build{};
  build.engine_path = dir;
  build.engine_options = opts;
  build.shard_count = 4;
  build.durability_tier = DurabilityClass::kBalanced;
  build.expect = &expect;
  build.probe_keys = CollectProbeKeysFromExpect(expect);

  RarReport report{};
  ASSERT_TRUE(BuildRar(build, &report).ok());
  EXPECT_EQ(report.verdict, RarVerdict::kPass);
  EXPECT_EQ(report.physical.shards.size(), 4u);
  EXPECT_EQ(report.recovery.probes.size(), 2u);
}

}  // namespace
}  // namespace audit
}  // namespace ebtree
