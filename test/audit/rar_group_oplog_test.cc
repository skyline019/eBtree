#include <fstream>
#include <gtest/gtest.h>

#include "catalog_expect.h"
#include "digest.h"
#include "op_log_expect.h"
#include "json_writer.h"
#include "rar_builder.h"
#include "rar_sign.h"
#if defined(EBTREE_RAR_SIGNING)
#define ED25519_NO_SEED
#include "third_party/ed25519/ed25519.h"
#endif
#include "engine_test_util.h"

namespace ebtree {
namespace audit {
namespace {

TEST(RarGroupOpLog, VerifyAfterGroupCommit) {
  const std::string dir = test::TempDir("rar_gc_oplog");
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
  EXPECT_GT(report.op_log.durable_entry_count, 0u);
}

TEST(RarSchemaConformance, V2FieldsPresent) {
  const std::string dir = test::TempDir("rar_schema_v2");
  BuildRarOptions opts{};
  opts.engine_path = dir;
  opts.op_log_path = dir + "/ebtree.op_log.jsonl";
  opts.catalog_path = dir + "/ebtree.catalog.json";
  {
    std::ofstream op(dir + "/ebtree.op_log.jsonl");
    op << "{\"v\":1,\"op\":\"put\",\"key\":\"1:k\",\"value_sha256\":\"abc\","
          "\"lsn\":1,\"durable_at_return\":true,\"tier\":\"balanced\","
          "\"ts_unix\":1}\n";
    std::ofstream cat(dir + "/ebtree.catalog.json");
    cat << "{\"v\":1,\"tables\":[{\"id\":1,\"name\":\"t\",\"key_column\":\"key\","
           "\"value_column\":\"value\"}]}\n";
  }

  RarReport report{};
  ASSERT_TRUE(BuildRar(opts, &report).ok());
  const std::string json = RarReportToJson(report);
  EXPECT_NE(json.find("\"rar_version\": \"2.0\""), std::string::npos);
  EXPECT_NE(json.find("\"attest_export_version\": 1"), std::string::npos);
  EXPECT_NE(json.find("\"catalog\""), std::string::npos);
  EXPECT_NE(json.find("\"op_log\""), std::string::npos);
}

TEST(RarSign, RoundTrip) {
  const std::string body = "{\"rar_version\":\"2.0\",\"verdict\":\"PASS\"}";
#if defined(EBTREE_RAR_SIGNING)
  unsigned char seed[32] = {};
  std::string sig;
  ASSERT_TRUE(SignRarJson(body, std::string(reinterpret_cast<char*>(seed), 32), &sig).ok());
  unsigned char public_key[32];
  unsigned char private_key[64];
  ed25519_create_keypair(public_key, private_key, seed);
  const std::string pubkey(reinterpret_cast<char*>(public_key), 32);
  EXPECT_TRUE(VerifyRarSignature(body, sig, pubkey).ok());
#else
  std::string sig;
  ASSERT_TRUE(SignRarJson(body, "test-secret", &sig).ok());
  EXPECT_TRUE(VerifyRarSignature(body, sig, "test-secret").ok());
#endif
}

}  // namespace
}  // namespace audit
}  // namespace ebtree
