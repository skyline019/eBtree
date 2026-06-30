#include <gtest/gtest.h>

#include <unordered_map>

#include "chaos_runner.h"
#include "engine_test_util.h"

TEST(EbFailureChaos, RandomOpsSurviveReopen) {
  const std::string dir = ebtree::test::TempDir("chaos_random");
  const auto ops = ebtree::test::GenerateChaosOps(42, 1000);
  std::unordered_map<std::string, std::string> expected;
  {
    auto engine = ebtree::test::OpenEngine(dir);
    ASSERT_NE(engine, nullptr);
    ASSERT_TRUE(ebtree::test::ExecuteChaosOps(engine.get(), ops, nullptr).ok());
    for (const auto& op : ops) {
      if (op.type == ebtree::test::ChaosOpType::kPut) {
        expected[op.key] = op.value;
      } else if (op.type == ebtree::test::ChaosOpType::kDelete) {
        expected.erase(op.key);
      }
    }
    ASSERT_TRUE(engine->Checkpoint().ok());
  }
  auto engine = ebtree::test::OpenEngine(dir);
  ASSERT_NE(engine, nullptr);
  for (const auto& kv : expected) {
    std::string value;
    const ebtree::Status st = engine->Get(kv.first, &value);
    ASSERT_TRUE(st.ok()) << kv.first;
    EXPECT_EQ(value, kv.second);
  }
  EXPECT_EQ(engine->stats().fallback_read_total, 0u);
}

TEST(EbFailureChaos, PowerfailAfterPut) {
  const std::string dir = ebtree::test::TempDir("chaos_powerfail_put");
  {
    auto engine =
        ebtree::test::OpenEngine(dir, ebtree::DurabilityClass::kSync);
    ASSERT_NE(engine, nullptr);
    ASSERT_TRUE(engine->Put("pf", "yes").ok());
  }
  auto engine = ebtree::test::OpenEngine(dir, ebtree::DurabilityClass::kSync);
  ASSERT_NE(engine, nullptr);
  std::string value;
  ASSERT_TRUE(engine->Get("pf", &value).ok());
  EXPECT_EQ(value, "yes");
}

TEST(EbFailureChaos, BadBlockRandomOffset) {
  const std::string dir = ebtree::test::TempDir("chaos_bad_block");
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
}

TEST(EbFailureChaos, PowerfailBeforeCheckpoint) {
  const std::string dir = ebtree::test::TempDir("chaos_pf_before_cp");
  {
    auto engine =
        ebtree::test::OpenEngine(dir, ebtree::DurabilityClass::kSync);
    ASSERT_NE(engine, nullptr);
    ASSERT_TRUE(engine->Put("pc1", "pv1").ok());
    ASSERT_TRUE(engine->Put("pc2", "pv2").ok());
  }
  auto engine = ebtree::test::OpenEngine(dir, ebtree::DurabilityClass::kSync);
  ASSERT_NE(engine, nullptr);
  std::string value;
  ASSERT_TRUE(engine->Get("pc1", &value).ok());
  EXPECT_EQ(value, "pv1");
  ASSERT_TRUE(engine->Get("pc2", &value).ok());
  EXPECT_EQ(value, "pv2");
}

TEST(EbFailureChaos, PowerfailAfterFlush) {
  const std::string dir = ebtree::test::TempDir("chaos_pf_after_flush");
  {
    auto engine = ebtree::test::OpenEngine(dir);
    ASSERT_NE(engine, nullptr);
    ASSERT_TRUE(engine->Put("fl1", "fv1").ok());
    ASSERT_TRUE(engine->Flush().ok());
  }
  auto engine = ebtree::test::OpenEngine(dir);
  ASSERT_NE(engine, nullptr);
  std::string value;
  ASSERT_TRUE(engine->Get("fl1", &value).ok());
  EXPECT_EQ(value, "fv1");
}

TEST(EbFailureChaos, PowerfailMidCheckpoint) {
  const std::string dir = ebtree::test::TempDir("chaos_pf_mid_cp");
  {
    auto engine = ebtree::test::OpenEngine(dir);
    ASSERT_NE(engine, nullptr);
    ASSERT_TRUE(engine->Put("mc1", "mv1").ok());
    ASSERT_TRUE(engine->Checkpoint().ok());
    ASSERT_TRUE(engine->Put("mc2", "mv2").ok());
  }
  auto engine = ebtree::test::OpenEngine(dir);
  ASSERT_NE(engine, nullptr);
  std::string value;
  ASSERT_TRUE(engine->Get("mc1", &value).ok());
  EXPECT_EQ(value, "mv1");
  ASSERT_TRUE(engine->Get("mc2", &value).ok());
  EXPECT_EQ(value, "mv2");
}

TEST(EbFailureChaos, FourShardRandomOps) {
  const std::string dir = ebtree::test::TempDir("chaos_4shard");
  ebtree::EngineOptions opts;
  opts.path = dir;
  opts.durability = ebtree::DurabilityClass::kSync;
  opts.sync_on_commit = true;
  opts.shard_count = 4;

  const auto ops = ebtree::test::GenerateChaosOps(99, 500);
  std::unordered_map<std::string, std::string> expected;
  {
    std::unique_ptr<ebtree::Engine> engine;
    ASSERT_TRUE(ebtree::Engine::Open(opts, &engine).ok());
    ASSERT_TRUE(ebtree::test::ExecuteChaosOps(engine.get(), ops, nullptr).ok());
    for (const auto& op : ops) {
      if (op.type == ebtree::test::ChaosOpType::kPut) {
        expected[op.key] = op.value;
      } else if (op.type == ebtree::test::ChaosOpType::kDelete) {
        expected.erase(op.key);
      }
    }
    ASSERT_TRUE(engine->Checkpoint().ok());
  }
  std::unique_ptr<ebtree::Engine> engine;
  ASSERT_TRUE(ebtree::Engine::Open(opts, &engine).ok());
  for (const auto& kv : expected) {
    std::string value;
    ASSERT_TRUE(engine->Get(kv.first, &value).ok());
    EXPECT_EQ(value, kv.second);
  }
}
