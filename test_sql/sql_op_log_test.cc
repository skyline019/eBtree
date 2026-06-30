#include <filesystem>
#include <fstream>
#include <gtest/gtest.h>

#include "engine_test_util.h"
#include "sql/session/database.h"

namespace ebtree {
namespace sql {
namespace {

TEST(SqlOpLog, ReopenWithAttestationPasses) {
  const std::string dir = test::TempDir("sql_oplog");
  const std::string op_log = dir + "/ebtree.op_log.jsonl";
  const std::string catalog = dir + "/ebtree.catalog.json";

  {
    OpenOptions opts{};
    opts.path = dir;
    opts.attestation = AttestationMode::kOff;
    std::unique_ptr<Database> db;
    ASSERT_TRUE(Database::Open(opts, &db).ok());
    ASSERT_TRUE(
        db->ExecuteSql("CREATE TABLE t (key TEXT PRIMARY KEY, value TEXT)").ok());
    ASSERT_TRUE(
        db->ExecuteSql("INSERT INTO t (key, value) VALUES ('k1', 'v1')").ok());
    db->Close();
    ASSERT_TRUE(std::filesystem::exists(op_log));
    ASSERT_TRUE(std::filesystem::exists(catalog));
  }

  OpenOptions reopen{};
  reopen.path = dir;
  reopen.attestation = AttestationMode::kRequirePass;
  reopen.recovery_max_missing = 0;
  std::unique_ptr<Database> db;
  EXPECT_TRUE(Database::Open(reopen, &db).ok());

  ExecuteResult result{};
  ASSERT_TRUE(
      db->ExecuteSql("SELECT key, value FROM t WHERE key = 'k1'", &result).ok());
  ASSERT_EQ(result.rows.size(), 1u);
  EXPECT_EQ(result.rows[0].value, "v1");
}

TEST(SqlOpLog, GroupCommitFlipsDurableBit) {
  const std::string dir = test::TempDir("sql_gc_oplog");
  const std::string op_log = dir + "/ebtree.op_log.jsonl";

  OpenOptions opts{};
  opts.path = dir;
  opts.durability = DurabilityClass::kGroup;
  opts.attestation = AttestationMode::kOff;
  std::unique_ptr<Database> db;
  ASSERT_TRUE(Database::Open(opts, &db).ok());
  ASSERT_TRUE(
      db->ExecuteSql("CREATE TABLE t (key TEXT PRIMARY KEY, value TEXT)").ok());
  ASSERT_TRUE(
      db->ExecuteSql("INSERT INTO t (key, value) VALUES ('gc1', 'v1')").ok());
  ASSERT_TRUE(db->engine()->GroupCommit().ok());
  db->Close();

  std::ifstream in(op_log);
  ASSERT_TRUE(in);
  std::string content((std::istreambuf_iterator<char>(in)),
                      std::istreambuf_iterator<char>());
  EXPECT_NE(content.find("\"durable_at_return\":true"), std::string::npos);
}

}  // namespace
}  // namespace sql
}  // namespace ebtree
