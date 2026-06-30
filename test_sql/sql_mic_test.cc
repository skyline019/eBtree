#include <gtest/gtest.h>

#include "engine_test_util.h"
#include "sql/exec/mic_guard.h"
#include "sql/session/database.h"

namespace ebtree {
namespace sql {
namespace {

TEST(SqlMic, PointSelectWithinBudget) {
  const std::string dir = test::TempDir("sql_mic_pass");
  OpenOptions opts{};
  opts.path = dir;
  opts.attestation = AttestationMode::kOff;

  std::unique_ptr<Database> db;
  ASSERT_TRUE(Database::Open(opts, &db).ok());
  ASSERT_TRUE(
      db->ExecuteSql("CREATE TABLE t (key TEXT PRIMARY KEY, value TEXT)").ok());
  ASSERT_TRUE(
      db->ExecuteSql("INSERT INTO t (key, value) VALUES ('k1', 'v1')").ok());

  ExecuteResult result{};
  const Status st = db->ExecuteSql(
      "SELECT key, value FROM t WHERE key = 'k1' /* @max_pages=32 */",
      &result);
  EXPECT_TRUE(st.ok());
  ASSERT_EQ(result.rows.size(), 1u);
}

TEST(SqlMic, TableScanOverBudgetRejected) {
  const std::string dir = test::TempDir("sql_mic_fail");
  OpenOptions opts{};
  opts.path = dir;
  opts.attestation = AttestationMode::kOff;

  std::unique_ptr<Database> db;
  ASSERT_TRUE(Database::Open(opts, &db).ok());
  ASSERT_TRUE(
      db->ExecuteSql("CREATE TABLE t (key TEXT PRIMARY KEY, value TEXT)").ok());
  for (int i = 0; i < 16; ++i) {
    const std::string sql = "INSERT INTO t (key, value) VALUES ('k" +
                            std::to_string(i) + "', 'v" +
                            std::to_string(i) + "')";
    ASSERT_TRUE(db->ExecuteSql(sql).ok());
  }

  ExecuteResult result{};
  const Status st = db->ExecuteSql(
      "SELECT key, value FROM t /* @max_pages=0 */", &result);
  EXPECT_FALSE(st.ok());
  EXPECT_TRUE(IsMicContractViolation(st));
}

TEST(SqlMic, RejectionDoesNotBlockReopen) {
  const std::string dir = test::TempDir("sql_mic_reopen");
  OpenOptions opts{};
  opts.path = dir;
  opts.attestation = AttestationMode::kRequirePass;

  {
    std::unique_ptr<Database> db;
    ASSERT_TRUE(Database::Open(opts, &db).ok());
    ASSERT_TRUE(
        db->ExecuteSql("CREATE TABLE t (key TEXT PRIMARY KEY, value TEXT)").ok());
    for (int i = 0; i < 16; ++i) {
      ASSERT_TRUE(db->ExecuteSql("INSERT INTO t (key, value) VALUES ('k" +
                                 std::to_string(i) + "', 'v')")
                      .ok());
    }
    ExecuteResult result{};
    EXPECT_FALSE(db->ExecuteSql("SELECT key, value FROM t /* @max_pages=0 */",
                                &result)
                     .ok());
    db->Close();
  }

  std::unique_ptr<Database> db2;
  EXPECT_TRUE(Database::Open(opts, &db2).ok());
}

TEST(SqlMic, JoinScanOverBudgetRejected) {
  const std::string dir = test::TempDir("sql_mic_join");
  OpenOptions opts{};
  opts.path = dir;
  std::unique_ptr<Database> db;
  ASSERT_TRUE(Database::Open(opts, &db).ok());
  ASSERT_TRUE(
      db->ExecuteSql("CREATE TABLE a (key TEXT PRIMARY KEY, value TEXT)").ok());
  ASSERT_TRUE(
      db->ExecuteSql("CREATE TABLE b (key TEXT PRIMARY KEY, value TEXT)").ok());
  for (int i = 0; i < 16; ++i) {
    const std::string k = "k" + std::to_string(i);
    ASSERT_TRUE(
        db->ExecuteSql("INSERT INTO a (key, value) VALUES ('" + k + "', 'v')")
            .ok());
    ASSERT_TRUE(
        db->ExecuteSql("INSERT INTO b (key, value) VALUES ('" + k + "', 'v')")
            .ok());
  }
  ExecuteResult result{};
  const Status st = db->ExecuteSql(
      "SELECT a.key FROM a JOIN b ON a.key = b.key /* @max_pages=0 */", &result);
  EXPECT_FALSE(st.ok());
  EXPECT_TRUE(IsMicContractViolation(st));
}

TEST(SqlMic, SubqueryScanWithinBudget) {
  const std::string dir = test::TempDir("sql_mic_subq");
  OpenOptions opts{};
  opts.path = dir;
  std::unique_ptr<Database> db;
  ASSERT_TRUE(Database::Open(opts, &db).ok());
  ASSERT_TRUE(
      db->ExecuteSql("CREATE TABLE t (key TEXT PRIMARY KEY, value TEXT)").ok());
  ASSERT_TRUE(
      db->ExecuteSql("INSERT INTO t (key, value) VALUES ('k1', 'v1')").ok());
  ExecuteResult result{};
  EXPECT_TRUE(db->ExecuteSql(
                  "SELECT key FROM t WHERE key IN (SELECT key FROM t) /* "
                  "@max_pages=32 */",
                  &result)
                  .ok());
}

}  // namespace
}  // namespace sql
}  // namespace ebtree
