#include <gtest/gtest.h>

#include <filesystem>

#include "ebtree/concept/superblock/superblock.h"
#include "ebtree/concept/tlog/tlog.h"
#include "engine_test_util.h"

TEST(EbPipelineTlog, CheckpointAdvancesTlogTail) {
  const std::string dir = ebtree::test::TempDir("tlog_tail");
  auto engine = ebtree::test::OpenEngine(dir);
  ASSERT_NE(engine, nullptr);
  ASSERT_TRUE(engine->Put("t1", "v1").ok());
  ASSERT_TRUE(engine->Checkpoint().ok());
  EXPECT_GE(engine->stats().tlog_snapshot_total, 1u);
  EXPECT_TRUE(std::filesystem::exists(dir + "/shard0.tlog"));
}

TEST(EbPipelineTlog, ReopenAfterCheckpointWithCorruptWal) {
  const std::string dir = ebtree::test::TempDir("tlog_wal_fallback");
  {
    auto engine = ebtree::test::OpenEngine(dir);
    ASSERT_NE(engine, nullptr);
    ASSERT_TRUE(engine->Put("persist", "yes").ok());
    ASSERT_TRUE(engine->Checkpoint().ok());
    ASSERT_TRUE(engine->Put("volatile", "no").ok());
    ASSERT_TRUE(engine->CorruptWalForTest().ok());
  }
  auto engine = ebtree::test::OpenEngine(dir);
  ASSERT_NE(engine, nullptr);
  std::string value;
  ASSERT_TRUE(engine->Get("persist", &value).ok());
  EXPECT_EQ(value, "yes");
  EXPECT_TRUE(ebtree::test::IsNotFound(engine->Get("volatile", &value)));
}

TEST(EbPipelineTlog, FlashbackAfterCorruptWal) {
  const std::string dir = ebtree::test::TempDir("tlog_flashback_corrupt");
  uint32_t ts0 = 9000;
  ebtree::SetTimestampSourceForTest([ts0]() { return ts0; });
  {
    auto engine = ebtree::test::OpenEngine(dir);
    ASSERT_NE(engine, nullptr);
    ASSERT_TRUE(engine->Put("fb", "snap").ok());
    ASSERT_TRUE(engine->Checkpoint().ok());
    ASSERT_TRUE(engine->CorruptWalForTest().ok());
  }
  auto engine = ebtree::test::OpenEngine(dir);
  ASSERT_NE(engine, nullptr);
  std::string value;
  ASSERT_TRUE(engine->GetAsOf("fb", ts0, &value).ok());
  EXPECT_EQ(value, "snap");
  EXPECT_EQ(engine->stats().fallback_read_total, 0u);
  ebtree::ResetTimestampSourceForTest();
}

TEST(EbPipelineTlog, IndexSidecarMatchesTlogEntries) {
  const std::string dir = ebtree::test::TempDir("tlog_index_parity");
  const std::string tlog_path = dir + "/shard0.tlog";
  const std::string idx_path = tlog_path + "idx";
  {
    auto engine = ebtree::test::OpenEngine(dir);
    ASSERT_NE(engine, nullptr);
    for (int i = 0; i < 3; ++i) {
      ASSERT_TRUE(engine->Put("idx_k" + std::to_string(i), "v").ok());
      ASSERT_TRUE(engine->Checkpoint().ok());
    }
  }
  ASSERT_TRUE(std::filesystem::exists(tlog_path));
  ASSERT_TRUE(std::filesystem::exists(idx_path));

  const auto tlog_size = std::filesystem::file_size(tlog_path);
  const auto idx_size = std::filesystem::file_size(idx_path);
  EXPECT_EQ(tlog_size / sizeof(ebtree::TLogEntry),
            idx_size / sizeof(ebtree::TLogIndexEntry));

  ebtree::TLogReader reader(tlog_path);
  std::vector<ebtree::TLogSnapshot> snaps;
  ASSERT_TRUE(reader.ListSnapshots(&snaps).ok());
  EXPECT_EQ(snaps.size(), 3u);
  for (const auto& snap : snaps) {
    EXPECT_GT(snap.data_lsn, 0u);
    EXPECT_GT(snap.timestamp_sec, 0u);
  }
}
