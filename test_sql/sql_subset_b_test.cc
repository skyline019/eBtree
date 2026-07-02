#include <gtest/gtest.h>

#include "engine_test_util.h"
#include "sql/session/database.h"

namespace ebtree {
namespace sql {
namespace {

TEST(SqlSubsetB, IndexScanEqRichSelect) {
  const std::string dir = test::TempDir("subset_b_index_eq");
  OpenOptions opts{};
  opts.path = dir;
  std::unique_ptr<Database> db;
  ASSERT_TRUE(Database::Open(opts, &db).ok());
  ASSERT_TRUE(
      db->ExecuteSql("CREATE TABLE t (key TEXT PRIMARY KEY, value TEXT)").ok());
  ASSERT_TRUE(
      db->ExecuteSql("INSERT INTO t (key, value) VALUES ('k1', 'v1')").ok());
  ASSERT_TRUE(db->ExecuteSql("CREATE INDEX idx_val ON t (value)").ok());
  ExecuteResult result{};
  ASSERT_TRUE(
      db->ExecuteSql("SELECT key, value FROM t WHERE value = 'v1'", &result)
          .ok());
  ASSERT_EQ(result.rows.size(), 1u);
  EXPECT_EQ(result.rows[0].value, "v1");
}

TEST(SqlSubsetB, IndexScanRangeRichSelect) {
  const std::string dir = test::TempDir("subset_b_index_range");
  OpenOptions opts{};
  opts.path = dir;
  std::unique_ptr<Database> db;
  ASSERT_TRUE(Database::Open(opts, &db).ok());
  ASSERT_TRUE(
      db->ExecuteSql("CREATE TABLE t (key TEXT PRIMARY KEY, value TEXT)").ok());
  ASSERT_TRUE(
      db->ExecuteSql("INSERT INTO t (key, value) VALUES ('a', '10')").ok());
  ASSERT_TRUE(
      db->ExecuteSql("INSERT INTO t (key, value) VALUES ('b', '20')").ok());
  ASSERT_TRUE(
      db->ExecuteSql("INSERT INTO t (key, value) VALUES ('c', '30')").ok());
  ASSERT_TRUE(db->ExecuteSql("CREATE INDEX idx_val ON t (value)").ok());
  ExecuteResult result{};
  ASSERT_TRUE(db->ExecuteSql(
                  "SELECT key FROM t WHERE value >= '15' AND value <= '25'",
                  &result)
                  .ok());
  ASSERT_EQ(result.rows.size(), 1u);
  EXPECT_EQ(result.rows[0].key, "b");
}

TEST(SqlSubsetB, CompositeIndexLeadingEq) {
  const std::string dir = test::TempDir("subset_b_composite_index");
  OpenOptions opts{};
  opts.path = dir;
  std::unique_ptr<Database> db;
  ASSERT_TRUE(Database::Open(opts, &db).ok());
  ASSERT_TRUE(db->ExecuteSql(
                  "CREATE TABLE t (key TEXT PRIMARY KEY, a TEXT, b TEXT)")
                  .ok());
  ASSERT_TRUE(
      db->ExecuteSql("INSERT INTO t (key, a, b) VALUES ('k1', 'x', '1')").ok());
  ASSERT_TRUE(
      db->ExecuteSql("INSERT INTO t (key, a, b) VALUES ('k2', 'x', '2')").ok());
  ASSERT_TRUE(
      db->ExecuteSql("INSERT INTO t (key, a, b) VALUES ('k3', 'y', '1')").ok());
  ASSERT_TRUE(db->ExecuteSql("CREATE INDEX idx_ab ON t (a, b)").ok());
  ExecuteResult result{};
  ASSERT_TRUE(
      db->ExecuteSql("SELECT key FROM t WHERE a = 'x'", &result).ok());
  ASSERT_EQ(result.rows.size(), 2u);
  ExecuteResult explain{};
  ASSERT_TRUE(db->ExecuteSql(
                  "EXPLAIN QUERY PLAN SELECT key FROM t WHERE a = 'x'",
                  &explain)
                  .ok());
  ASSERT_EQ(explain.rows.size(), 1u);
  EXPECT_NE(explain.rows[0].value.find("USING INDEX a"), std::string::npos);
}

TEST(SqlSubsetB, ExplainMatchesIndexScan) {
  const std::string dir = test::TempDir("subset_b_explain");
  OpenOptions opts{};
  opts.path = dir;
  std::unique_ptr<Database> db;
  ASSERT_TRUE(Database::Open(opts, &db).ok());
  ASSERT_TRUE(
      db->ExecuteSql("CREATE TABLE t (key TEXT PRIMARY KEY, value TEXT)").ok());
  ASSERT_TRUE(db->ExecuteSql("CREATE INDEX idx_val ON t (value)").ok());
  ExecuteResult result{};
  ASSERT_TRUE(db->ExecuteSql(
                  "EXPLAIN QUERY PLAN SELECT key FROM t WHERE value = 'x'",
                  &result)
                  .ok());
  ASSERT_EQ(result.rows.size(), 1u);
  EXPECT_NE(result.rows[0].value.find("USING INDEX"), std::string::npos);
}

TEST(SqlSubsetB, LikeFilter) {
  const std::string dir = test::TempDir("subset_b_like");
  OpenOptions opts{};
  opts.path = dir;
  std::unique_ptr<Database> db;
  ASSERT_TRUE(Database::Open(opts, &db).ok());
  ASSERT_TRUE(
      db->ExecuteSql("CREATE TABLE t (key TEXT PRIMARY KEY, value TEXT)").ok());
  ASSERT_TRUE(
      db->ExecuteSql("INSERT INTO t (key, value) VALUES ('a', 'hello')").ok());
  ASSERT_TRUE(
      db->ExecuteSql("INSERT INTO t (key, value) VALUES ('b', 'world')").ok());
  ExecuteResult result{};
  ASSERT_TRUE(
      db->ExecuteSql("SELECT key FROM t WHERE value LIKE 'hel%'", &result).ok());
  ASSERT_EQ(result.rows.size(), 1u);
  EXPECT_EQ(result.rows[0].key, "a");
}

TEST(SqlSubsetB, CastInteger) {
  const std::string dir = test::TempDir("subset_b_cast");
  OpenOptions opts{};
  opts.path = dir;
  std::unique_ptr<Database> db;
  ASSERT_TRUE(Database::Open(opts, &db).ok());
  ASSERT_TRUE(
      db->ExecuteSql("CREATE TABLE t (key TEXT PRIMARY KEY, value TEXT)").ok());
  ASSERT_TRUE(
      db->ExecuteSql("INSERT INTO t (key, value) VALUES ('1', '42')").ok());
  ExecuteResult result{};
  ASSERT_TRUE(db->ExecuteSql(
                  "SELECT CAST(value AS INTEGER) AS v FROM t WHERE key = '1'",
                  &result)
                  .ok());
  ASSERT_EQ(result.rows.size(), 1u);
  EXPECT_EQ(result.rows[0].value, "42");
}

TEST(SqlSubsetB, CheckConstraintInsertFail) {
  const std::string dir = test::TempDir("subset_b_check");
  OpenOptions opts{};
  opts.path = dir;
  std::unique_ptr<Database> db;
  ASSERT_TRUE(Database::Open(opts, &db).ok());
  ASSERT_TRUE(db->ExecuteSql(
                  "CREATE TABLE t (key TEXT PRIMARY KEY, x INTEGER CHECK(x > 0))")
                  .ok());
  EXPECT_FALSE(
      db->ExecuteSql("INSERT INTO t (key, x) VALUES ('k', 0)").ok());
  ASSERT_TRUE(
      db->ExecuteSql("INSERT INTO t (key, x) VALUES ('k', 1)").ok());
}

TEST(SqlSubsetB, InsertOrIgnore) {
  const std::string dir = test::TempDir("subset_b_ignore");
  OpenOptions opts{};
  opts.path = dir;
  std::unique_ptr<Database> db;
  ASSERT_TRUE(Database::Open(opts, &db).ok());
  ASSERT_TRUE(
      db->ExecuteSql("CREATE TABLE t (key TEXT PRIMARY KEY, value TEXT)").ok());
  ASSERT_TRUE(
      db->ExecuteSql("INSERT INTO t (key, value) VALUES ('k', 'v1')").ok());
  ASSERT_TRUE(
      db->ExecuteSql("INSERT OR IGNORE INTO t (key, value) VALUES ('k', 'v2')")
          .ok());
  ExecuteResult result{};
  ASSERT_TRUE(db->ExecuteSql("SELECT value FROM t WHERE key = 'k'", &result).ok());
  ASSERT_EQ(result.rows.size(), 1u);
  EXPECT_EQ(result.rows[0].value, "v1");
}

TEST(SqlSubsetB, RollbackToSavepoint) {
  const std::string dir = test::TempDir("subset_b_sp_rollback");
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
  ASSERT_TRUE(db->ExecuteSql("ROLLBACK TO SAVEPOINT sp1").ok());
  ASSERT_TRUE(db->ExecuteSql("COMMIT").ok());

  ExecuteResult result{};
  ASSERT_TRUE(db->ExecuteSql("SELECT key FROM t ORDER BY key", &result).ok());
  ASSERT_EQ(result.rows.size(), 1u);
  EXPECT_EQ(result.rows[0].key, "k1");
}

}  // namespace
}  // namespace sql
}  // namespace ebtree
