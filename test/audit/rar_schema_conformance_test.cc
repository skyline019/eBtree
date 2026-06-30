#include <gtest/gtest.h>

#include "rar_builder.h"
#include "engine_test_util.h"

namespace ebtree {
namespace audit {
namespace {

TEST(RarSkipPhysical, BadwalDetectedOnOpenPath) {
  const std::string dir = test::TempDir("rar_skip_phys");
  {
    auto engine = test::OpenEngine(dir, DurabilityClass::kBalanced);
    ASSERT_TRUE(engine);
    ASSERT_TRUE(engine->Put("k", "v").ok());
    ASSERT_TRUE(engine->CorruptWalForTest().ok());
  }

  BuildRarOptions opts{};
  opts.engine_path = dir;
  opts.skip_physical_if_engine_open = true;
  std::unique_ptr<Engine> engine;
  RarReport report{};
  ASSERT_TRUE(BuildRar(opts, &report, &engine).ok());
  EXPECT_TRUE(engine);
  bool badwal = false;
  for (const auto& shard : report.physical.shards) {
    if (shard.wal.badwal_marker) badwal = true;
  }
  EXPECT_TRUE(badwal);
}

}  // namespace
}  // namespace audit
}  // namespace ebtree
