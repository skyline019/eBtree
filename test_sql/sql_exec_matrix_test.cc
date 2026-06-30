#include <gtest/gtest.h>

#include "engine_test_util.h"
#include "sql/session/database.h"

namespace ebtree {
namespace sql {
namespace {

TEST(SqlExecMatrix, UpdateDeleteDropDml) {
  const std::string dir = test::TempDir("sql_exec_matrix");
  OpenOptions opts{};
  opts.path = dir;
  std::unique_ptr<Database> db;
  ASSERT_TRUE(Database::Open(opts, &db).ok());

  ASSERT_TRUE(
      db->ExecuteSql("CREATE TABLE t (key TEXT PRIMARY KEY, value TEXT)").ok());
  ASSERT_TRUE(
      db->ExecuteSql("INSERT INTO t (key, value) VALUES ('k1', 'v1')").ok());
  ASSERT_TRUE(
      db->ExecuteSql("UPDATE t SET value = 'v2' WHERE key = 'k1'").ok());

  ExecuteResult sel{};
  ASSERT_TRUE(
      db->ExecuteSql("SELECT key, value FROM t WHERE key = 'k1'", &sel).ok());
  ASSERT_EQ(sel.rows.size(), 1u);
  EXPECT_EQ(sel.rows[0].value, "v2");

  ASSERT_TRUE(db->ExecuteSql("DELETE FROM t WHERE key = 'k1'").ok());
  sel.rows.clear();
  ASSERT_TRUE(
      db->ExecuteSql("SELECT key, value FROM t WHERE key = 'k1'", &sel).ok());
  EXPECT_TRUE(sel.rows.empty());

  ASSERT_TRUE(db->ExecuteSql("DROP TABLE t").ok());
}

TEST(SqlExecMatrix, BulkDmlRoundTrips) {
  for (int i = 0; i < 40; ++i) {
    const std::string dir = test::TempDir("sql_exec_bulk_" + std::to_string(i));
    OpenOptions opts{};
    opts.path = dir;
    std::unique_ptr<Database> db;
    ASSERT_TRUE(Database::Open(opts, &db).ok());
    const std::string table = "t" + std::to_string(i);
    ASSERT_TRUE(
        db->ExecuteSql("CREATE TABLE " + table +
                       " (key TEXT PRIMARY KEY, value TEXT)").ok());
    const std::string key = "k" + std::to_string(i);
    const std::string val = "v" + std::to_string(i);
    ASSERT_TRUE(
        db->ExecuteSql("INSERT INTO " + table + " (key, value) VALUES ('" + key +
                       "', '" + val + "')").ok());
    ASSERT_TRUE(
        db->ExecuteSql("UPDATE " + table + " SET value = 'v2' WHERE key = '" +
                       key + "'").ok());
    ExecuteResult sel{};
    ASSERT_TRUE(
        db->ExecuteSql("SELECT key, value FROM " + table + " WHERE key = '" +
                       key + "'", &sel).ok());
    EXPECT_EQ(sel.rows.size(), 1u);
    EXPECT_EQ(sel.rows[0].value, "v2");
    ASSERT_TRUE(
        db->ExecuteSql("DELETE FROM " + table + " WHERE key = '" + key + "'").ok());
  }
}

}  // namespace
}  // namespace sql
}  // namespace ebtree
