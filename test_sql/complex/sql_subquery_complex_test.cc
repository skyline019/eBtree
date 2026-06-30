#include <gtest/gtest.h>

#include "engine_test_util.h"
#include "sql/session/database.h"

namespace ebtree {
namespace sql {
namespace {

void SeedGroupTable(Database* db) {
  ASSERT_TRUE(
      db->ExecuteSql("CREATE TABLE t (key TEXT PRIMARY KEY, value TEXT)").ok());
  ASSERT_TRUE(
      db->ExecuteSql("INSERT INTO t (key, value) VALUES ('a', '1')").ok());
  ASSERT_TRUE(
      db->ExecuteSql("INSERT INTO t (key, value) VALUES ('b', '1')").ok());
  ASSERT_TRUE(
      db->ExecuteSql("INSERT INTO t (key, value) VALUES ('c', '2')").ok());
}

TEST(SqlComplex, GroupByHavingCount) {
  const std::string dir = test::TempDir("sql_having");
  OpenOptions opts{};
  opts.path = dir;
  std::unique_ptr<Database> db;
  ASSERT_TRUE(Database::Open(opts, &db).ok());
  SeedGroupTable(db.get());

  ExecuteResult result{};
  ASSERT_TRUE(db->ExecuteSql(
                  "SELECT value, COUNT(*) AS cnt FROM t GROUP BY value HAVING cnt "
                  "> 1",
                  &result)
                  .ok());
  EXPECT_EQ(result.rows.size(), 1u);
}

TEST(SqlComplex, NotInEmptySubquery) {
  const std::string dir = test::TempDir("sql_not_in_empty");
  OpenOptions opts{};
  opts.path = dir;
  std::unique_ptr<Database> db;
  ASSERT_TRUE(Database::Open(opts, &db).ok());
  SeedGroupTable(db.get());

  ExecuteResult result{};
  ASSERT_TRUE(
      db->ExecuteSql(
          "SELECT key FROM t WHERE key NOT IN (SELECT key FROM t WHERE key = 'z')",
          &result)
          .ok());
  EXPECT_EQ(result.rows.size(), 3u);
}

TEST(SqlComplex, JoinWithInWhere) {
  const std::string dir = test::TempDir("sql_join_in");
  OpenOptions opts{};
  opts.path = dir;
  std::unique_ptr<Database> db;
  ASSERT_TRUE(Database::Open(opts, &db).ok());
  ASSERT_TRUE(
      db->ExecuteSql("CREATE TABLE a (key TEXT PRIMARY KEY, value TEXT)").ok());
  ASSERT_TRUE(
      db->ExecuteSql("CREATE TABLE b (key TEXT PRIMARY KEY, value TEXT)").ok());
  ASSERT_TRUE(
      db->ExecuteSql("INSERT INTO a (key, value) VALUES ('k1', '1')").ok());
  ASSERT_TRUE(
      db->ExecuteSql("INSERT INTO b (key, value) VALUES ('k1', '1')").ok());

  ExecuteResult result{};
  ASSERT_TRUE(db->ExecuteSql(
                  "SELECT a.key FROM a JOIN b ON a.key = b.key WHERE a.key IN "
                  "(SELECT key FROM b)",
                  &result)
                  .ok());
  EXPECT_EQ(result.rows.size(), 1u);
}

TEST(SqlComplex, ScalarSubquerySelect) {
  const std::string dir = test::TempDir("sql_scalar");
  OpenOptions opts{};
  opts.path = dir;
  std::unique_ptr<Database> db;
  ASSERT_TRUE(Database::Open(opts, &db).ok());
  SeedGroupTable(db.get());

  ExecuteResult result{};
  ASSERT_TRUE(
      db->ExecuteSql(
          "SELECT (SELECT key FROM t WHERE key = 'a') AS k FROM t WHERE key = 'a'",
          &result)
          .ok());
  EXPECT_GE(result.rows.size(), 1u);
}

TEST(SqlComplex, NestedExists) {
  const std::string dir = test::TempDir("sql_nested_exists");
  OpenOptions opts{};
  opts.path = dir;
  std::unique_ptr<Database> db;
  ASSERT_TRUE(Database::Open(opts, &db).ok());
  ASSERT_TRUE(
      db->ExecuteSql("CREATE TABLE a (key TEXT PRIMARY KEY, value TEXT)").ok());
  ASSERT_TRUE(
      db->ExecuteSql("CREATE TABLE b (key TEXT PRIMARY KEY, value TEXT)").ok());
  ASSERT_TRUE(
      db->ExecuteSql("INSERT INTO a (key, value) VALUES ('k1', '1')").ok());
  ASSERT_TRUE(
      db->ExecuteSql("INSERT INTO b (key, value) VALUES ('k1', '1')").ok());

  ExecuteResult result{};
  ASSERT_TRUE(db->ExecuteSql(
                  "SELECT a.key FROM a WHERE EXISTS (SELECT b.key FROM b WHERE "
                  "b.key = a.key AND EXISTS (SELECT a.key FROM a WHERE a.key = "
                  "b.key))",
                  &result)
                  .ok());
  EXPECT_EQ(result.rows.size(), 1u);
}

}  // namespace
}  // namespace sql
}  // namespace ebtree
