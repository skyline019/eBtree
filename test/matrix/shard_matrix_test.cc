#include <gtest/gtest.h>

#include "engine_test_util.h"
#include "matrix_test_runner.h"
#include "shard_matrix_inc.h"

namespace {

class ShardMatrixTest : public ::testing::TestWithParam<int> {};

void RunMultishardReopen(const EbMatrixCase& c, uint32_t shard_count) {
  const std::string dir = ebtree::test::TempDir("matrix_" + c.id);
  ebtree::EngineOptions opts;
  opts.path = dir;
  opts.durability = ebtree::test::ParseDurability(c.durability);
  opts.sync_on_commit = true;
  opts.shard_count = shard_count;
  {
    std::unique_ptr<ebtree::Engine> engine;
    ASSERT_TRUE(ebtree::Engine::Open(opts, &engine).ok());
    for (const auto& op : c.setup_ops) {
      ebtree::test::ApplyMatrixOp(engine.get(), op);
    }
    ASSERT_TRUE(engine->Checkpoint().ok());
  }
  std::unique_ptr<ebtree::Engine> engine;
  ASSERT_TRUE(ebtree::Engine::Open(opts, &engine).ok());
  if (!c.get_key.empty()) {
    std::string value;
    ASSERT_TRUE(engine->Get(c.get_key, &value).ok());
    EXPECT_EQ(value, c.get_value);
  }
  EXPECT_EQ(engine->stats().fallback_read_total, 0u);
}

}  // namespace

TEST_P(ShardMatrixTest, RunCase) {
  const auto& c = kShardMatrixCases[GetParam()];
  if (c.run == "multishard_reopen") {
    RunMultishardReopen(c, 4);
    return;
  }
  if (c.run == "multishard16_reopen") {
    RunMultishardReopen(c, 16);
    return;
  }
  if (c.run == "multishard256_reopen") {
    RunMultishardReopen(c, 256);
    return;
  }
  ebtree::test::RunMatrixCase(c);
}

INSTANTIATE_TEST_SUITE_P(EbMatrix, ShardMatrixTest,
                         ::testing::Range(0, kShardMatrixCaseCount));

TEST(EbMatrixSchema, ShardCasesNonEmpty) {
  EXPECT_GT(kShardMatrixCaseCount, 0);
}
