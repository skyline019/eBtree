#include <gtest/gtest.h>

#include "engine_test_util.h"
#include "sql/exec/mic_guard.h"
#include "sql/session/database.h"

namespace ebtree {
namespace sql {
namespace {

TEST(SqlSubqueryDebt, DepthExceededReturnsError) {
  const std::string dir = test::TempDir("sql_depth_exceeded");
  OpenOptions opts{};
  opts.path = dir;
  std::unique_ptr<Database> db;
  ASSERT_TRUE(Database::Open(opts, &db).ok());
  ASSERT_TRUE(
      db->ExecuteSql("CREATE TABLE t (key TEXT PRIMARY KEY, value TEXT)").ok());

  const Status st = db->ExecuteSql(
      "SELECT key FROM t WHERE key IN (SELECT key FROM t WHERE key IN "
      "(SELECT key FROM t WHERE key IN (SELECT key FROM t)))");
  EXPECT_FALSE(st.ok());
  EXPECT_NE(st.message().find("subquery depth exceeded"), std::string::npos);
}

}  // namespace
}  // namespace sql
}  // namespace ebtree
