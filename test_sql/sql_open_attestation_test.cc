#include <fstream>
#include <gtest/gtest.h>

#include "engine_test_util.h"
#include "sql/session/database.h"

namespace ebtree {
namespace sql {
namespace {

TEST(SqlOpenAttestation, CleanDatabasePasses) {
  const std::string dir = test::TempDir("sql_attest_pass");
  {
    OpenOptions seed{};
    seed.path = dir;
    seed.attestation = AttestationMode::kOff;
    std::unique_ptr<Database> db;
    ASSERT_TRUE(Database::Open(seed, &db).ok());
    ASSERT_TRUE(
        db->ExecuteSql("CREATE TABLE t (key TEXT PRIMARY KEY, value TEXT)").ok());
    ASSERT_TRUE(
        db->ExecuteSql("INSERT INTO t (key, value) VALUES ('k1', 'v1')").ok());
    db->Close();
  }

  OpenOptions opts{};
  opts.path = dir;
  opts.attestation = AttestationMode::kRequirePass;
  std::unique_ptr<Database> db;
  EXPECT_TRUE(Database::Open(opts, &db).ok());
}

TEST(SqlOpenAttestation, CorruptWalRefusesStart) {
  const std::string dir = test::TempDir("sql_attest_corrupt");
  {
    auto engine = test::OpenEngine(dir, DurabilityClass::kBalanced);
    ASSERT_TRUE(engine);
    ASSERT_TRUE(engine->Put("bad_k", "bad_v").ok());
    ASSERT_TRUE(engine->CorruptWalForTest().ok());
  }

  OpenOptions opts{};
  opts.path = dir;
  opts.attestation = AttestationMode::kRequirePass;
  std::unique_ptr<Database> db;
  const Status st = Database::Open(opts, &db);
  EXPECT_FALSE(st.ok());
  EXPECT_EQ(st.code(), StatusCode::kCorrupt);
}

TEST(SqlOpenAttestation, MissingKeysExceedMaxMissing) {
  const std::string dir = test::TempDir("sql_attest_missing");
  const std::string op_log = dir + "/ebtree.op_log.jsonl";
  {
    std::ofstream out(op_log, std::ios::trunc);
    ASSERT_TRUE(out);
    out << "{\"v\":1,\"op\":\"put\",\"key\":\"1:ghost\",\"value_sha256\":\"abc\","
           "\"lsn\":1,\"durable_at_return\":true,\"tier\":\"balanced\","
           "\"ts_unix\":1}\n";
  }

  OpenOptions opts{};
  opts.path = dir;
  opts.attestation = AttestationMode::kRequirePass;
  opts.recovery_max_missing = 0;
  std::unique_ptr<Database> db;
  const Status st = Database::Open(opts, &db);
  EXPECT_FALSE(st.ok());
}

TEST(SqlOpenAttestation, RequirePassRefusesPendingWarn) {
  const std::string dir = test::TempDir("sql_warn_require");
  const std::string op_log = dir + "/ebtree.op_log.jsonl";

  {
    OpenOptions seed{};
    seed.path = dir;
    seed.durability = DurabilityClass::kGroup;
    seed.attestation = AttestationMode::kOff;
    std::unique_ptr<Database> db;
    ASSERT_TRUE(Database::Open(seed, &db).ok());
    ASSERT_TRUE(
        db->ExecuteSql("CREATE TABLE t (key TEXT PRIMARY KEY, value TEXT)").ok());
    ASSERT_TRUE(
        db->ExecuteSql("INSERT INTO t (key, value) VALUES ('k1', 'v1')").ok());
    db->Close();
  }

  {
    std::ofstream out(op_log, std::ios::trunc);
    ASSERT_TRUE(out);
    out << "{\"v\":1,\"op\":\"put\",\"key\":\"1:k1\",\"value_sha256\":\"abc\","
           "\"lsn\":1,\"durable_at_return\":false,\"tier\":\"group\","
           "\"ts_unix\":1}\n";
  }

  OpenOptions opts{};
  opts.path = dir;
  opts.durability = DurabilityClass::kGroup;
  opts.attestation = AttestationMode::kRequirePass;
  std::unique_ptr<Database> db;
  const Status st = Database::Open(opts, &db);
  EXPECT_FALSE(st.ok());
}

TEST(SqlOpenAttestation, AllowWarnPermitsPending) {
  const std::string dir = test::TempDir("sql_warn_allow");
  const std::string op_log = dir + "/ebtree.op_log.jsonl";

  {
    OpenOptions seed{};
    seed.path = dir;
    seed.durability = DurabilityClass::kGroup;
    seed.attestation = AttestationMode::kOff;
    std::unique_ptr<Database> db;
    ASSERT_TRUE(Database::Open(seed, &db).ok());
    ASSERT_TRUE(
        db->ExecuteSql("CREATE TABLE t (key TEXT PRIMARY KEY, value TEXT)").ok());
    ASSERT_TRUE(
        db->ExecuteSql("INSERT INTO t (key, value) VALUES ('k1', 'v1')").ok());
    db->Close();
  }

  {
    std::ofstream out(op_log, std::ios::trunc);
    ASSERT_TRUE(out);
    out << "{\"v\":1,\"op\":\"put\",\"key\":\"1:k1\",\"value_sha256\":\"abc\","
           "\"lsn\":1,\"durable_at_return\":false,\"tier\":\"group\","
           "\"ts_unix\":1}\n";
  }

  OpenOptions opts{};
  opts.path = dir;
  opts.durability = DurabilityClass::kGroup;
  opts.attestation = AttestationMode::kAllowWarn;
  std::unique_ptr<Database> db;
  EXPECT_TRUE(Database::Open(opts, &db).ok());
}

}  // namespace
}  // namespace sql
}  // namespace ebtree
