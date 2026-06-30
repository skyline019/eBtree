#include <gtest/gtest.h>

#include "engine_test_util.h"
#include "sql/session/database.h"

namespace ebtree {
namespace sql {
namespace {

TEST(SqlSmoke, CreateInsertSelectRoundTrip) {
  const std::string dir = test::TempDir("sql_smoke");
  OpenOptions opts{};
  opts.path = dir;
  opts.durability = DurabilityClass::kBalanced;
  opts.attestation = AttestationMode::kOff;

  std::unique_ptr<Database> db;
  ASSERT_TRUE(Database::Open(opts, &db).ok());

  ASSERT_TRUE(
      db->ExecuteSql("CREATE TABLE t (key TEXT PRIMARY KEY, value TEXT)").ok());
  ASSERT_TRUE(db->ExecuteSql("INSERT INTO t (key, value) VALUES ('k1', 'v1')")
                  .ok());

  ExecuteResult result{};
  ASSERT_TRUE(
      db->ExecuteSql("SELECT key, value FROM t WHERE key = 'k1'", &result).ok());
  ASSERT_EQ(result.rows.size(), 1u);
  EXPECT_EQ(result.rows[0].key, "k1");
  EXPECT_EQ(result.rows[0].value, "v1");
}

}  // namespace
}  // namespace sql
}  // namespace ebtree
