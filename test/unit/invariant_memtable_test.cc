#include <gtest/gtest.h>

#include "engine_test_util.h"

TEST(EbInvariantMemTable, RotatePreservesKeys) {
  const std::string dir = ebtree::test::TempDir("inv_mt_rotate");
  auto engine = ebtree::test::OpenEngine(dir);
  ASSERT_NE(engine, nullptr);
  ASSERT_TRUE(engine->Put("keep", "value").ok());
  engine->RotateMemTableForFlush();
  std::string value;
  ASSERT_TRUE(engine->Get("keep", &value).ok());
  EXPECT_EQ(value, "value");
}

TEST(EbInvariantMemTable, CheckpointClearsAllStages) {
  const std::string dir = ebtree::test::TempDir("inv_mt_checkpoint");
  auto engine = ebtree::test::OpenEngine(dir);
  ASSERT_NE(engine, nullptr);
  ASSERT_TRUE(engine->Put("k", "v").ok());
  ASSERT_TRUE(engine->Checkpoint().ok());
  EXPECT_TRUE(engine->memtable()->Snapshot().empty());
  EXPECT_TRUE(engine->immutable_memtable()->Snapshot().empty());
  EXPECT_TRUE(engine->flushing_memtable()->Snapshot().empty());
  EXPECT_FALSE(engine->committed().empty());
}
