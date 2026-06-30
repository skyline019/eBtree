#include <gtest/gtest.h>

#include "sql/parse/native/native_parser.h"
#include "sql/session/database.h"
#include "engine_test_util.h"

namespace ebtree {
namespace sql {
namespace {

TEST(SqlFailure, UnknownTableExec) {
  const std::string dir = test::TempDir("sql_unknown_table");
  OpenOptions opts{};
  opts.path = dir;
  std::unique_ptr<Database> db;
  ASSERT_TRUE(Database::Open(opts, &db).ok());
  const Status st = db->ExecuteSql("SELECT key FROM missing");
  EXPECT_FALSE(st.ok());
}

TEST(SqlFailure, CteExecOk) {
  const std::string dir = test::TempDir("sql_cte_ok");
  OpenOptions opts{};
  opts.path = dir;
  std::unique_ptr<Database> db;
  ASSERT_TRUE(Database::Open(opts, &db).ok());
  ASSERT_TRUE(
      db->ExecuteSql("CREATE TABLE t (key TEXT PRIMARY KEY, value TEXT)").ok());
  ASSERT_TRUE(
      db->ExecuteSql("INSERT INTO t (key, value) VALUES ('k1', 'v1')").ok());
  ExecuteResult result{};
  const Status st = db->ExecuteSql(
      "WITH cte AS (SELECT key FROM t) SELECT key FROM cte", &result);
  EXPECT_TRUE(st.ok()) << st.message();
  EXPECT_EQ(result.rows.size(), 1u);
}

TEST(SqlFailure, ParseInvalidSyntax) {
  parse::NativeParser parser;
  QueryStatement stmt{};
  EXPECT_FALSE(parser.Parse("SELECT FROM t", &stmt).ok());
}

}  // namespace
}  // namespace sql
}  // namespace ebtree
