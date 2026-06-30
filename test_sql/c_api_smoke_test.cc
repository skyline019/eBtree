#include <gtest/gtest.h>

#include "ebtree_sql.h"
#include "engine_test_util.h"

#include <filesystem>

namespace {

std::string TempDir(const std::string& name) {
  return ebtree::test::TempDir(name);
}

}  // namespace

TEST(EbtreeSqlCApi, OpenExecuteClose) {
  const std::string dir = TempDir("sql_capi");
  ebtree_sql_open_options_t opts{};
  opts.path = dir.c_str();
  opts.durability = "balanced";
  opts.attestation_mode = EBTREE_SQL_ATTEST_OFF;
  opts.recovery_max_missing = 0;
  opts.op_log_path = nullptr;

  ebtree_sql_db* db = nullptr;
  ASSERT_EQ(ebtree_sql_open(&opts, &db), 0);
  ASSERT_NE(db, nullptr);

  ASSERT_EQ(ebtree_sql_execute(
                db, "CREATE TABLE t (key TEXT PRIMARY KEY, value TEXT)", nullptr),
            0);
  ASSERT_EQ(
      ebtree_sql_execute(db, "INSERT INTO t (key, value) VALUES ('k1', 'v1')",
                         nullptr),
      0);

  ebtree_sql_result_t result{};
  ASSERT_EQ(ebtree_sql_execute(
                db, "SELECT key, value FROM t WHERE key = 'k1'", &result),
            0);
  ASSERT_EQ(result.count, 1u);
  EXPECT_STREQ(result.rows[0].key, "k1");
  EXPECT_STREQ(result.rows[0].value, "v1");
  ebtree_sql_result_free(&result);

  ebtree_sql_close(db);
  EXPECT_TRUE(std::filesystem::exists(dir));
}
