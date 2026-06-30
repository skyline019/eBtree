#include <gtest/gtest.h>

#include "rar_builder.h"
#include "engine_test_util.h"

namespace ebtree {
namespace audit {
namespace {

TEST(RarMultishardInferred, PerShardPathsPresent) {
  const std::string dir = test::TempDir("rar_ms_inf");
  EngineOptions opts = EngineOptions::ProductionDefaults(dir);
  opts.path = dir;
  opts.shard_count = 4;

  {
    std::unique_ptr<Engine> engine;
    ASSERT_TRUE(Engine::Open(opts, &engine).ok());
    ASSERT_TRUE(engine->Put("k0", "v0").ok());
  }

  BuildRarOptions build{};
  build.engine_path = dir;
  build.engine_options = opts;
  build.shard_count = 4;

  RarReport report{};
  ASSERT_TRUE(BuildRar(build, &report).ok());
  EXPECT_EQ(report.physical.shards.size(), 4u);
  ASSERT_FALSE(report.recovery.shard_state.empty());
  EXPECT_NE(report.recovery.inferred_path, InferredRecoveryPath::kUnknown);
  for (const auto& shard : report.recovery.shard_state) {
    EXPECT_NE(shard.inferred_path, InferredRecoveryPath::kUnknown);
  }
}

}  // namespace
}  // namespace audit
}  // namespace ebtree
