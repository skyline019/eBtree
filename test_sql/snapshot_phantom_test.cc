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

TEST(SnapshotPhantomSql, InsertDuringActiveRangeScanConflicts) {
  const std::string dir = test::TempDir("sql_phantom");
  OpenOptions opts = TestDbOpts(dir);
  std::unique_ptr<Database> setup;
  ASSERT_TRUE(Database::Open(opts, &setup).ok());
  ASSERT_TRUE(
      setup->ExecuteSql("CREATE TABLE t (key TEXT PRIMARY KEY, value TEXT)")
          .ok());

  std::unique_ptr<Database> scanner;
  std::unique_ptr<Database> inserter;
  ASSERT_TRUE(Database::Open(opts, &scanner).ok());
  ASSERT_TRUE(Database::Open(opts, &inserter).ok());
  ASSERT_TRUE(scanner->ExecuteSql("BEGIN").ok());
  ASSERT_TRUE(inserter->ExecuteSql("BEGIN").ok());

  ExecuteResult scan{};
  ASSERT_TRUE(scanner->ExecuteSql("SELECT key FROM t", &scan).ok());
  EXPECT_EQ(scan.rows.size(), 0u);

  ASSERT_TRUE(
      inserter
          ->ExecuteSql("INSERT INTO t (key, value) VALUES ('phantom', 'v')")
          .ok());
  const Status commit = inserter->ExecuteSql("COMMIT");
  EXPECT_FALSE(commit.ok());
}

}  // namespace
}  // namespace sql
}  // namespace ebtree
