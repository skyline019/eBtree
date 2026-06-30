#include <gtest/gtest.h>

#include "ebtree/concept/datafile/datafile.h"
#include "engine_test_util.h"

TEST(EbInvariantRecovery, FullReplayDoesNotDeferWal) {
  const std::string dir = ebtree::test::TempDir("inv_full_replay");
  {
    auto engine = ebtree::test::OpenEngine(dir);
    ASSERT_NE(engine, nullptr);
    ASSERT_TRUE(engine->Put("ir1", "iv1").ok());
  }
  ebtree::EngineOptions opts;
  opts.path = dir;
  opts.durability = ebtree::DurabilityClass::kSync;
  opts.recovery_strategy = ebtree::RecoveryStrategy::kFullReplay;
  std::unique_ptr<ebtree::Engine> engine;
  ASSERT_TRUE(ebtree::Engine::Open(opts, &engine).ok());
  ASSERT_NE(engine, nullptr);
  EXPECT_FALSE(engine->wal_replay_pending());
}

TEST(EbInvariantRecovery, BadBlockDoesNotTriggerFallbackRead) {
  const std::string dir = ebtree::test::TempDir("inv_bad_block");
  {
    auto engine = ebtree::test::OpenEngine(dir);
    ASSERT_NE(engine, nullptr);
    ASSERT_TRUE(engine->Put("bb1", "bv1").ok());
    ASSERT_TRUE(engine->Put("bb2", "bv2").ok());
    ASSERT_TRUE(engine->Checkpoint().ok());
    const uint64_t corrupt_offset = sizeof(ebtree::DataRecordHeader) +
                                    std::string("bb1").size() +
                                    std::string("bv1").size();
    ASSERT_TRUE(engine->CorruptDataFileForTest(corrupt_offset).ok());
  }
  auto engine = ebtree::test::OpenEngine(dir);
  ASSERT_NE(engine, nullptr);
  std::string value;
  ASSERT_TRUE(engine->Get("bb1", &value).ok());
  EXPECT_EQ(value, "bv1");
  EXPECT_EQ(engine->stats().fallback_read_total, 0u);
}
