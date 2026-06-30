#include <gtest/gtest.h>

#include "engine_test_util.h"
#include "sql/session/database.h"

namespace ebtree {
namespace sql {
namespace {

void SetupTwoTables(Database* db) {
  ASSERT_TRUE(
      db->ExecuteSql("CREATE TABLE a (key TEXT PRIMARY KEY, value TEXT)").ok());
  ASSERT_TRUE(
      db->ExecuteSql("CREATE TABLE b (key TEXT PRIMARY KEY, value TEXT)").ok());
  ASSERT_TRUE(
      db->ExecuteSql("INSERT INTO a (key, value) VALUES ('k1', '1')").ok());
  ASSERT_TRUE(
      db->ExecuteSql("INSERT INTO a (key, value) VALUES ('k2', '2')").ok());
  ASSERT_TRUE(
      db->ExecuteSql("INSERT INTO b (key, value) VALUES ('k1', '1')").ok());
  ASSERT_TRUE(
      db->ExecuteSql("INSERT INTO b (key, value) VALUES ('k3', '3')").ok());
}

TEST(SqlJoinSubquery, TwoTableInnerJoin) {
  const std::string dir = test::TempDir("sql_join_inner");
  OpenOptions opts{};
  opts.path = dir;
  std::unique_ptr<Database> db;
  ASSERT_TRUE(Database::Open(opts, &db).ok());
  SetupTwoTables(db.get());

  ExecuteResult result{};
  ASSERT_TRUE(db->ExecuteSql(
                  "SELECT a.key FROM a JOIN b ON a.key = b.key WHERE a.key = 'k1'",
                  &result)
                  .ok());
  EXPECT_EQ(result.rows.size(), 1u);
  EXPECT_EQ(result.rows[0].key, "k1");
}

TEST(SqlJoinSubquery, LeftJoinPreservesUnmatched) {
  const std::string dir = test::TempDir("sql_join_left");
  OpenOptions opts{};
  opts.path = dir;
  std::unique_ptr<Database> db;
  ASSERT_TRUE(Database::Open(opts, &db).ok());
  SetupTwoTables(db.get());

  ExecuteResult result{};
  ASSERT_TRUE(db->ExecuteSql(
                  "SELECT a.key FROM a LEFT JOIN b ON a.key = b.key WHERE a.key = 'k2'",
                  &result)
                  .ok());
  EXPECT_EQ(result.rows.size(), 1u);
  EXPECT_EQ(result.rows[0].key, "k2");
}

TEST(SqlJoinSubquery, ThreeTableJoinChain) {
  const std::string dir = test::TempDir("sql_join_three");
  OpenOptions opts{};
  opts.path = dir;
  std::unique_ptr<Database> db;
  ASSERT_TRUE(Database::Open(opts, &db).ok());
  ASSERT_TRUE(
      db->ExecuteSql("CREATE TABLE a (key TEXT PRIMARY KEY, value TEXT)").ok());
  ASSERT_TRUE(
      db->ExecuteSql("CREATE TABLE b (key TEXT PRIMARY KEY, value TEXT)").ok());
  ASSERT_TRUE(
      db->ExecuteSql("CREATE TABLE c (key TEXT PRIMARY KEY, value TEXT)").ok());
  ASSERT_TRUE(
      db->ExecuteSql("INSERT INTO a (key, value) VALUES ('k1', '1')").ok());
  ASSERT_TRUE(
      db->ExecuteSql("INSERT INTO b (key, value) VALUES ('k1', '1')").ok());
  ASSERT_TRUE(
      db->ExecuteSql("INSERT INTO c (key, value) VALUES ('k1', '1')").ok());

  ExecuteResult result{};
  ASSERT_TRUE(db->ExecuteSql(
                  "SELECT a.key FROM a JOIN b ON a.key = b.key JOIN c ON b.key = c.key",
                  &result)
                  .ok());
  EXPECT_EQ(result.rows.size(), 1u);
  EXPECT_EQ(result.rows[0].key, "k1");
}

TEST(SqlJoinSubquery, WhereInUncorrelatedSubquery) {
  const std::string dir = test::TempDir("sql_in_subq");
  OpenOptions opts{};
  opts.path = dir;
  std::unique_ptr<Database> db;
  ASSERT_TRUE(Database::Open(opts, &db).ok());
  SetupTwoTables(db.get());

  ExecuteResult result{};
  ASSERT_TRUE(db->ExecuteSql(
                  "SELECT a.key FROM a WHERE a.key IN (SELECT b.key FROM b)",
                  &result)
                  .ok());
  EXPECT_EQ(result.rows.size(), 1u);
  EXPECT_EQ(result.rows[0].key, "k1");
}

TEST(SqlJoinSubquery, WhereExistsCorrelated) {
  const std::string dir = test::TempDir("sql_exists_corr");
  OpenOptions opts{};
  opts.path = dir;
  std::unique_ptr<Database> db;
  ASSERT_TRUE(Database::Open(opts, &db).ok());
  SetupTwoTables(db.get());

  ExecuteResult result{};
  ASSERT_TRUE(db->ExecuteSql(
                  "SELECT a.key FROM a WHERE EXISTS (SELECT b.key FROM b WHERE b.key = a.key)",
                  &result)
                  .ok());
  EXPECT_EQ(result.rows.size(), 1u);
  EXPECT_EQ(result.rows[0].key, "k1");
}

TEST(SqlJoinSubquery, GroupBySelect) {
  const std::string dir = test::TempDir("sql_join_group");
  OpenOptions opts{};
  opts.path = dir;
  std::unique_ptr<Database> db;
  ASSERT_TRUE(Database::Open(opts, &db).ok());
  ASSERT_TRUE(
      db->ExecuteSql("CREATE TABLE t (key TEXT PRIMARY KEY, value TEXT)").ok());
  ASSERT_TRUE(
      db->ExecuteSql("INSERT INTO t (key, value) VALUES ('a', '1')").ok());
  ASSERT_TRUE(
      db->ExecuteSql("INSERT INTO t (key, value) VALUES ('b', '1')").ok());

  ExecuteResult result{};
  ASSERT_TRUE(db->ExecuteSql(
                  "SELECT key, value FROM t GROUP BY key ORDER BY key LIMIT 10",
                  &result)
                  .ok());
  EXPECT_GE(result.rows.size(), 1u);
}

TEST(SqlTxnParse, BeginCommitExec) {
  const std::string dir = test::TempDir("sql_txn_exec");
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
}

}  // namespace
}  // namespace sql
}  // namespace ebtree
