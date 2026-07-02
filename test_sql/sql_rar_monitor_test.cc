#include <gtest/gtest.h>

#include "engine_test_util.h"
#include "sql/session/database.h"

namespace ebtree {
namespace sql {
namespace {

TEST(SqlRarMonitor, DefaultOpenIsMonitor) {
  const std::string dir = test::TempDir("sql_rar_default_monitor");
  OpenOptions opts{};
  opts.path = dir;
  std::unique_ptr<Database> db;
  ASSERT_TRUE(Database::Open(opts, &db).ok());
  EXPECT_EQ(db->options().attestation, AttestationMode::kMonitor);
  EXPECT_TRUE(db->rar_monitor().WorkerRunning());
  EXPECT_TRUE(
      db->ExecuteSql("CREATE TABLE t (key TEXT PRIMARY KEY, value TEXT)").ok());
  EXPECT_TRUE(
      db->ExecuteSql("INSERT INTO t (key, value) VALUES ('k1', 'v1')").ok());
}

TEST(SqlRarMonitor, PragmaRarStatus) {
  const std::string dir = test::TempDir("sql_rar_pragma_status");
  OpenOptions opts{};
  opts.path = dir;
  opts.attestation = AttestationMode::kMonitor;
  std::unique_ptr<Database> db;
  ASSERT_TRUE(Database::Open(opts, &db).ok());
  ExecuteResult result{};
  ASSERT_TRUE(db->ExecuteSql("PRAGMA rar_status", &result).ok());
  ASSERT_FALSE(result.rows.empty());
  bool saw_allows = false;
  for (const auto& row : result.rows) {
    if (row.key == "allows_write") {
      saw_allows = true;
      EXPECT_EQ(row.value, "1");
    }
  }
  EXPECT_TRUE(saw_allows);
}

TEST(SqlRarMonitor, WriteCircuitOpenOnUnexpectedPath) {
  const std::string dir = test::TempDir("sql_rar_monitor");
  OpenOptions opts{};
  opts.path = dir;
  opts.attestation = AttestationMode::kMonitor;
  std::unique_ptr<Database> db;
  ASSERT_TRUE(Database::Open(opts, &db).ok());
  ASSERT_TRUE(
      db->ExecuteSql("CREATE TABLE t (key TEXT PRIMARY KEY, value TEXT)").ok());

  if (EngineStats* stats = db->engine()->mutable_stats()) {
    ++stats->unexpected_path_total;
  }

  const Status write_st =
      db->ExecuteSql("INSERT INTO t (key, value) VALUES ('k1', 'v1')");
  EXPECT_FALSE(write_st.ok());
  EXPECT_EQ(write_st.code(), StatusCode::kCorrupt);

  ExecuteResult result{};
  const Status read_st = db->ExecuteSql("SELECT key, value FROM t", &result);
  EXPECT_TRUE(read_st.ok());
}

TEST(SqlRarMonitor, SmokeOpenWithMonitor) {
  const std::string dir = test::TempDir("sql_rar_monitor_smoke");
  OpenOptions opts{};
  opts.path = dir;
  opts.attestation = AttestationMode::kMonitor;
  std::unique_ptr<Database> db;
  EXPECT_TRUE(Database::Open(opts, &db).ok());
  EXPECT_TRUE(
      db->ExecuteSql("CREATE TABLE t (key TEXT PRIMARY KEY, value TEXT)").ok());
  EXPECT_TRUE(
      db->ExecuteSql("INSERT INTO t (key, value) VALUES ('k1', 'v1')").ok());
}

}  // namespace
}  // namespace sql
}  // namespace ebtree
