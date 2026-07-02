#include <gtest/gtest.h>

#include "engine_test_util.h"
#include "sql/session/database.h"

namespace ebtree {
namespace sql {
namespace {

OpenOptions TestDbOpts(const std::string& dir) {
  OpenOptions opts{};
  opts.path = dir;
  opts.attestation = AttestationMode::kOff;
  return opts;
}

TEST(SnapshotOccSql, ConcurrentUpdateSecondCommitConflicts) {
  const std::string dir = test::TempDir("sql_occ_update");
  OpenOptions opts = TestDbOpts(dir);
  std::unique_ptr<Database> setup;
  ASSERT_TRUE(Database::Open(opts, &setup).ok());
  ASSERT_TRUE(
      setup->ExecuteSql("CREATE TABLE t (key TEXT PRIMARY KEY, value TEXT)")
          .ok());
  ASSERT_TRUE(
      setup->ExecuteSql("INSERT INTO t (key, value) VALUES ('k', 'base')").ok());
  ASSERT_TRUE(setup->engine()->Checkpoint().ok());

  std::unique_ptr<Database> a;
  std::unique_ptr<Database> b;
  ASSERT_TRUE(Database::Open(opts, &a).ok());
  ASSERT_TRUE(Database::Open(opts, &b).ok());
  ASSERT_TRUE(a->ExecuteSql("BEGIN").ok());
  ASSERT_TRUE(b->ExecuteSql("BEGIN").ok());

  ExecuteResult read{};
  ASSERT_TRUE(a->ExecuteSql("SELECT value FROM t WHERE key = 'k'", &read).ok());
  ASSERT_EQ(read.rows.size(), 1u);

  ASSERT_TRUE(
      b->ExecuteSql("UPDATE t SET value = 'from_b' WHERE key = 'k'").ok());
  ASSERT_TRUE(b->ExecuteSql("COMMIT").ok());

  const Status upd =
      a->ExecuteSql("UPDATE t SET value = 'from_a' WHERE key = 'k'");
  ASSERT_TRUE(upd.ok());
  const Status commit = a->ExecuteSql("COMMIT");
  EXPECT_FALSE(commit.ok());
}

}  // namespace
}  // namespace sql
}  // namespace ebtree
