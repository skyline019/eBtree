#include <gtest/gtest.h>

#include "engine_test_util.h"
#include "sql/session/database.h"

namespace ebtree {
namespace sql {
namespace {

TEST(SqlTxn, CommitPersistsInsert) {
  const std::string dir = test::TempDir("sql_txn_commit");
  OpenOptions opts{};
  opts.path = dir;
  std::unique_ptr<Database> db;
  ASSERT_TRUE(Database::Open(opts, &db).ok());
  ASSERT_TRUE(
      db->ExecuteSql("CREATE TABLE t (key TEXT PRIMARY KEY, value TEXT)").ok());
  ASSERT_TRUE(db->ExecuteSql("BEGIN").ok());
  ASSERT_TRUE(
      db->ExecuteSql("INSERT INTO t (key, value) VALUES ('k1', 'v1')").ok());
  ASSERT_TRUE(db->ExecuteSql("COMMIT").ok());

  ExecuteResult result{};
  ASSERT_TRUE(db->ExecuteSql("SELECT key FROM t WHERE key = 'k1'", &result).ok());
  EXPECT_EQ(result.rows.size(), 1u);
}

TEST(SqlTxn, RollbackUndoesInsert) {
  const std::string dir = test::TempDir("sql_txn_rollback");
  OpenOptions opts{};
  opts.path = dir;
  std::unique_ptr<Database> db;
  ASSERT_TRUE(Database::Open(opts, &db).ok());
  ASSERT_TRUE(
      db->ExecuteSql("CREATE TABLE t (key TEXT PRIMARY KEY, value TEXT)").ok());
  ASSERT_TRUE(db->ExecuteSql("BEGIN").ok());
  ASSERT_TRUE(
      db->ExecuteSql("INSERT INTO t (key, value) VALUES ('k1', 'v1')").ok());
  ASSERT_TRUE(db->ExecuteSql("ROLLBACK").ok());

  ExecuteResult result{};
  ASSERT_TRUE(db->ExecuteSql("SELECT key FROM t WHERE key = 'k1'", &result).ok());
  EXPECT_EQ(result.rows.size(), 0u);
}

TEST(SqlTxn, SavepointThenRollback) {
  const std::string dir = test::TempDir("sql_txn_savepoint");
  OpenOptions opts{};
  opts.path = dir;
  std::unique_ptr<Database> db;
  ASSERT_TRUE(Database::Open(opts, &db).ok());
  ASSERT_TRUE(
      db->ExecuteSql("CREATE TABLE t (key TEXT PRIMARY KEY, value TEXT)").ok());
  ASSERT_TRUE(db->ExecuteSql("BEGIN").ok());
  ASSERT_TRUE(
      db->ExecuteSql("INSERT INTO t (key, value) VALUES ('k1', 'v1')").ok());
  ASSERT_TRUE(db->ExecuteSql("SAVEPOINT sp1").ok());
  ASSERT_TRUE(
      db->ExecuteSql("INSERT INTO t (key, value) VALUES ('k2', 'v2')").ok());
  ASSERT_TRUE(db->ExecuteSql("ROLLBACK").ok());

  ExecuteResult result{};
  ASSERT_TRUE(db->ExecuteSql("SELECT key FROM t", &result).ok());
  EXPECT_EQ(result.rows.size(), 0u);
}

}  // namespace
}  // namespace sql
}  // namespace ebtree
