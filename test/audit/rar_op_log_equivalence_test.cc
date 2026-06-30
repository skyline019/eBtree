#include <fstream>
#include <gtest/gtest.h>

#include "digest.h"
#include "op_log_expect.h"
#include "rar_builder.h"
#include "engine_test_util.h"
#include "powerfail_fuzz.h"

namespace ebtree {
namespace audit {
namespace {

void WriteOpLogEntry(std::ofstream* out, const std::string& key,
                     const std::string& value, bool durable) {
  *out << "{\"v\":1,\"op\":\"put\",\"key\":\"" << key << "\",\"value_sha256\":\""
       << Sha256HexString(value) << "\",\"lsn\":1,\"durable_at_return\":"
       << (durable ? "true" : "false")
       << ",\"tier\":\"balanced\",\"ts_unix\":1}\n";
}

TEST(RarOpLogEquivalence, OpLogExpectMatchesOracleDurable) {
  const std::string dir = test::TempDir("rar_oplog_eq");
  const std::string op_log = dir + "/ebtree.op_log.jsonl";
  test::CommittedOracle oracle(DurabilityClass::kBalanced);

  {
    auto engine = test::OpenEngine(dir, DurabilityClass::kBalanced);
    ASSERT_TRUE(engine);
    ASSERT_TRUE(engine->Put("ok1", "ov1").ok());
    oracle.OnPutOk(engine.get(), "ok1", "ov1");
    ASSERT_TRUE(engine->Put("ok2", "ov2").ok());
    oracle.OnPutOk(engine.get(), "ok2", "ov2");
  }

  {
    std::ofstream out(op_log, std::ios::trunc);
    ASSERT_TRUE(out);
    WriteOpLogEntry(&out, "ok1", "ov1", true);
    WriteOpLogEntry(&out, "ok2", "ov2", true);
  }

  ExpectSnapshot op_log_expect{};
  ASSERT_TRUE(
      LoadOpLogExpectSnapshot(op_log, ContractMode::kDurable, &op_log_expect)
          .ok());
  EXPECT_EQ(op_log_expect.entries.size(), oracle.durable_kv().size());

  BuildRarOptions opts{};
  opts.engine_path = dir;
  opts.durability_tier = DurabilityClass::kBalanced;
  opts.expect = &op_log_expect;
  opts.probe_keys = CollectProbeKeysFromExpect(op_log_expect);

  RarReport report{};
  ASSERT_TRUE(BuildRar(opts, &report).ok());
  EXPECT_EQ(report.verdict, RarVerdict::kPass);
  EXPECT_TRUE(report.contract.missing.empty());
}

}  // namespace
}  // namespace audit
}  // namespace ebtree
