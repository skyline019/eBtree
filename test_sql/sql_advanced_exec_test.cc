#include <gtest/gtest.h>

#include "engine_test_util.h"
#include "sql/session/database.h"

namespace ebtree {
namespace sql {
namespace {

TEST(SqlSetOpExec, UnionDistinct) {
  const std::string dir = test::TempDir("sql_setop_union");
  OpenOptions opts{};
  opts.path = dir;
  std::unique_ptr<Database> db;
  ASSERT_TRUE(Database::Open(opts, &db).ok());
  ASSERT_TRUE(
      db->ExecuteSql("CREATE TABLE t1 (key TEXT PRIMARY KEY, value TEXT)").ok());
  ASSERT_TRUE(
      db->ExecuteSql("CREATE TABLE t2 (key TEXT PRIMARY KEY, value TEXT)").ok());
  ASSERT_TRUE(
      db->ExecuteSql("INSERT INTO t1 (key, value) VALUES ('a', '1')").ok());
  ASSERT_TRUE(
      db->ExecuteSql("INSERT INTO t2 (key, value) VALUES ('a', '1')").ok());
  ASSERT_TRUE(
      db->ExecuteSql("INSERT INTO t2 (key, value) VALUES ('b', '2')").ok());

  ExecuteResult result{};
  ASSERT_TRUE(
      db->ExecuteSql("SELECT key FROM t1 UNION SELECT key FROM t2", &result).ok());
  EXPECT_EQ(result.rows.size(), 2u);
}

TEST(SqlCteExec, SimpleCteSelect) {
  const std::string dir = test::TempDir("sql_cte_exec");
  OpenOptions opts{};
  opts.path = dir;
  std::unique_ptr<Database> db;
  ASSERT_TRUE(Database::Open(opts, &db).ok());
  ASSERT_TRUE(
      db->ExecuteSql("CREATE TABLE t (key TEXT PRIMARY KEY, value TEXT)").ok());
  ASSERT_TRUE(
      db->ExecuteSql("INSERT INTO t (key, value) VALUES ('k1', 'v1')").ok());

  ExecuteResult result{};
  ASSERT_TRUE(
      db->ExecuteSql("WITH cte AS (SELECT key FROM t) SELECT key FROM cte",
                     &result)
          .ok());
  EXPECT_EQ(result.rows.size(), 1u);
}

TEST(SqlWindowExec, RowNumber) {
  const std::string dir = test::TempDir("sql_window_rn");
  OpenOptions opts{};
  opts.path = dir;
  std::unique_ptr<Database> db;
  ASSERT_TRUE(Database::Open(opts, &db).ok());
  ASSERT_TRUE(
      db->ExecuteSql("CREATE TABLE t (key TEXT PRIMARY KEY, value TEXT)").ok());
  ASSERT_TRUE(
      db->ExecuteSql("INSERT INTO t (key, value) VALUES ('a', '1')").ok());
  ASSERT_TRUE(
      db->ExecuteSql("INSERT INTO t (key, value) VALUES ('b', '2')").ok());

  ExecuteResult result{};
  ASSERT_TRUE(
      db->ExecuteSql("SELECT ROW_NUMBER() OVER (ORDER BY key) FROM t", &result)
          .ok());
  EXPECT_EQ(result.rows.size(), 2u);
  EXPECT_EQ(result.rows[0].value, "1");
  EXPECT_EQ(result.rows[1].value, "2");
}

}  // namespace
}  // namespace sql
}  // namespace ebtree
