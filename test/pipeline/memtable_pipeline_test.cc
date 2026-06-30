#include <gtest/gtest.h>

#include "engine_test_util.h"

TEST(EbPipelineMemTable, RotateDuringFlushStillReadable) {
  const std::string dir = ebtree::test::TempDir("mt_rotate_read");
  auto engine = ebtree::test::OpenEngine(dir);
  ASSERT_NE(engine, nullptr);
  ASSERT_TRUE(engine->Put("before", "bv").ok());
  engine->RotateMemTableForFlush();
  ASSERT_TRUE(engine->Put("after", "av").ok());

  std::string value;
  ASSERT_TRUE(engine->Get("before", &value).ok());
  EXPECT_EQ(value, "bv");
  ASSERT_TRUE(engine->Get("after", &value).ok());
  EXPECT_EQ(value, "av");
}

TEST(EbPipelineMemTable, FlushMakesDataDurable) {
  const std::string dir = ebtree::test::TempDir("mt_flush_durable");
  {
    auto engine = ebtree::test::OpenEngine(dir);
    ASSERT_NE(engine, nullptr);
    ASSERT_TRUE(engine->Put("durable", "yes").ok());
    ASSERT_TRUE(engine->Flush().ok());
    EXPECT_TRUE(engine->flushing_memtable()->Snapshot().empty());
  }
  auto engine = ebtree::test::OpenEngine(dir);
  ASSERT_NE(engine, nullptr);
  std::string value;
  ASSERT_TRUE(engine->Get("durable", &value).ok());
  EXPECT_EQ(value, "yes");
}

TEST(EbPipelineMemTable, DoubleFlushPreservesKeys) {
  const std::string dir = ebtree::test::TempDir("mt_double_flush");
  auto engine = ebtree::test::OpenEngine(dir);
  ASSERT_NE(engine, nullptr);
  ASSERT_TRUE(engine->Put("k1", "v1").ok());
  ASSERT_TRUE(engine->Flush().ok());
  ASSERT_TRUE(engine->Put("k2", "v2").ok());
  ASSERT_TRUE(engine->Flush().ok());

  std::string value;
  ASSERT_TRUE(engine->Get("k1", &value).ok());
  EXPECT_EQ(value, "v1");
  ASSERT_TRUE(engine->Get("k2", &value).ok());
  EXPECT_EQ(value, "v2");
}

TEST(EbPipelineMemTable, ImmutableEmptyDuringFlush) {
  const std::string dir = ebtree::test::TempDir("mt_immutable_empty");
  auto engine = ebtree::test::OpenEngine(dir);
  ASSERT_NE(engine, nullptr);
  ASSERT_TRUE(engine->Put("x", "y").ok());
  engine->RotateMemTableForFlush();
  EXPECT_TRUE(engine->immutable_memtable()->Snapshot().empty());
  EXPECT_FALSE(engine->flushing_memtable()->Snapshot().empty());
}
