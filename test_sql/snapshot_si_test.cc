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

TEST(SnapshotSiSql, RollbackRestoresSnapshotRead) {
  const std::string dir = test::TempDir("sql_si_rollback_snap");
  OpenOptions opts = TestDbOpts(dir);
  std::unique_ptr<Database> db;
  ASSERT_TRUE(Database::Open(opts, &db).ok());
  ASSERT_TRUE(
      db->ExecuteSql("CREATE TABLE t (key TEXT PRIMARY KEY, value TEXT)").ok());
  ASSERT_TRUE(
      db->ExecuteSql("INSERT INTO t (key, value) VALUES ('1', 'base')").ok());
  ASSERT_TRUE(db->ExecuteSql("BEGIN").ok());
  ASSERT_TRUE(
      db->ExecuteSql("UPDATE t SET value = 'dirty' WHERE key = '1'").ok());

  ExecuteResult during{};
  ASSERT_TRUE(
      db->ExecuteSql("SELECT value FROM t WHERE key = '1'", &during).ok());
  ASSERT_EQ(during.rows.size(), 1u);
  EXPECT_EQ(during.rows[0].value, "dirty");

  ASSERT_TRUE(db->ExecuteSql("ROLLBACK").ok());

  ExecuteResult after{};
  ASSERT_TRUE(
      db->ExecuteSql("SELECT value FROM t WHERE key = '1'", &after).ok());
  ASSERT_EQ(after.rows.size(), 1u);
  EXPECT_EQ(after.rows[0].value, "base");
}

TEST(SnapshotSiSql, UncommittedInsertVisibleToSelfOnly) {
  const std::string dir = test::TempDir("sql_si_self_insert");
  OpenOptions opts = TestDbOpts(dir);
  std::unique_ptr<Database> db;
  ASSERT_TRUE(Database::Open(opts, &db).ok());
  ASSERT_TRUE(
      db->ExecuteSql("CREATE TABLE t (key TEXT PRIMARY KEY, value TEXT)").ok());
  ASSERT_TRUE(db->ExecuteSql("BEGIN").ok());
  ASSERT_TRUE(
      db->ExecuteSql("INSERT INTO t (key, value) VALUES ('k', 'v')").ok());

  ExecuteResult during{};
  ASSERT_TRUE(db->ExecuteSql("SELECT key FROM t", &during).ok());
  EXPECT_EQ(during.rows.size(), 1u);

  ASSERT_TRUE(db->ExecuteSql("ROLLBACK").ok());

  ExecuteResult after{};
  ASSERT_TRUE(db->ExecuteSql("SELECT key FROM t", &after).ok());
  EXPECT_EQ(after.rows.size(), 0u);
}

TEST(SnapshotSiSql, TableScanSeesOwnUncommittedUpdate) {
  const std::string dir = test::TempDir("sql_si_scan_own");
  OpenOptions opts = TestDbOpts(dir);
  std::unique_ptr<Database> db;
  ASSERT_TRUE(Database::Open(opts, &db).ok());
  ASSERT_TRUE(
      db->ExecuteSql("CREATE TABLE t (key TEXT PRIMARY KEY, value TEXT)").ok());
  ASSERT_TRUE(
      db->ExecuteSql("INSERT INTO t (key, value) VALUES ('a', 'base')").ok());
  ASSERT_TRUE(db->ExecuteSql("BEGIN").ok());
  ASSERT_TRUE(
      db->ExecuteSql("UPDATE t SET value = 'dirty' WHERE key = 'a'").ok());

  ExecuteResult scan{};
  ASSERT_TRUE(db->ExecuteSql("SELECT value FROM t", &scan).ok());
  ASSERT_EQ(scan.rows.size(), 1u);
  EXPECT_EQ(scan.rows[0].value, "dirty");
}

TEST(SnapshotSiSql, ReadWriteSiSameSnapshot) {
  const std::string dir = test::TempDir("sql_si_rw_snap");
  OpenOptions opts = TestDbOpts(dir);
  std::unique_ptr<Database> outer;
  ASSERT_TRUE(Database::Open(opts, &outer).ok());
  ASSERT_TRUE(
      outer->ExecuteSql("CREATE TABLE t (key TEXT PRIMARY KEY, value TEXT)")
          .ok());
  ASSERT_TRUE(
      outer->ExecuteSql("INSERT INTO t (key, value) VALUES ('k', 'base')").ok());
  ASSERT_TRUE(outer->engine()->Checkpoint().ok());

  std::unique_ptr<Database> writer;
  ASSERT_TRUE(Database::Open(opts, &writer).ok());
  ASSERT_TRUE(writer->ExecuteSql("BEGIN").ok());
  std::unique_ptr<Database> other;
  ASSERT_TRUE(Database::Open(opts, &other).ok());
  ASSERT_TRUE(other->ExecuteSql("BEGIN").ok());
  ASSERT_TRUE(
      other->ExecuteSql("UPDATE t SET value = 'other' WHERE key = 'k'").ok());
  ASSERT_TRUE(other->ExecuteSql("COMMIT").ok());

  ExecuteResult before{};
  ASSERT_TRUE(
      writer->ExecuteSql("SELECT value FROM t WHERE key = 'k'", &before).ok());
  EXPECT_EQ(before.rows[0].value, "base");

  const Status upd =
      writer->ExecuteSql("UPDATE t SET value = 'self' WHERE key = 'k'");
  ASSERT_TRUE(upd.ok());
  const Status commit = writer->ExecuteSql("COMMIT");
  EXPECT_FALSE(commit.ok());
}

TEST(SnapshotSiSql, HidesOtherTxnUncommittedWrite) {
  const std::string dir = test::TempDir("sql_si_cross_db");
  OpenOptions opts = TestDbOpts(dir);
  std::unique_ptr<Database> writer;
  ASSERT_TRUE(Database::Open(opts, &writer).ok());
  ASSERT_TRUE(
      writer->ExecuteSql("CREATE TABLE t (key TEXT PRIMARY KEY, value TEXT)")
          .ok());
  ASSERT_TRUE(
      writer->ExecuteSql("INSERT INTO t (key, value) VALUES ('x', 'base')")
          .ok());
  ASSERT_TRUE(writer->engine()->Checkpoint().ok());
  ASSERT_TRUE(writer->ExecuteSql("BEGIN").ok());
  ASSERT_TRUE(
      writer->ExecuteSql("UPDATE t SET value = 'dirty' WHERE key = 'x'").ok());

  std::unique_ptr<Database> reader;
  ASSERT_TRUE(Database::Open(opts, &reader).ok());
  ASSERT_TRUE(reader->ExecuteSql("BEGIN").ok());

  ExecuteResult rows{};
  ASSERT_TRUE(
      reader->ExecuteSql("SELECT value FROM t WHERE key = 'x'", &rows).ok());
  ASSERT_EQ(rows.rows.size(), 1u);
  EXPECT_EQ(rows.rows[0].value, "base");
}

}  // namespace
}  // namespace sql
}  // namespace ebtree
