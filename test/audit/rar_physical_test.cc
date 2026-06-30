#include <gtest/gtest.h>

#include "engine_test_util.h"
#include "physical_attestor.h"
#include "rar_builder.h"

namespace ebtree {
namespace audit {
namespace {

TEST(RarPhysical, ValidSuperBlockAfterPutAndReopen) {
  const std::string dir = test::TempDir("rar_phys_put");
  {
    auto engine = test::OpenEngine(dir, DurabilityClass::kBalanced);
    ASSERT_TRUE(engine);
    ASSERT_TRUE(engine->Put("k1", "v1").ok());
  }

  PhysicalReport physical{};
  ASSERT_TRUE(PhysicalAttest(dir, 1, &physical).ok());
  ASSERT_EQ(physical.shards.size(), 1u);
  EXPECT_TRUE(physical.shards[0].superblock.valid);
  EXPECT_TRUE(physical.shards[0].invariants.data_lsn_le_wal_lsn);
  EXPECT_FALSE(physical.shards[0].digests.wal_sha256.empty());
  EXPECT_GE(physical.shards[0].reconstructed_key_count, 1u);
}

TEST(RarPhysical, WalCorruptUsesTLogFallbackReconstruction) {
  const std::string dir = test::TempDir("rar_phys_badwal");
  {
    auto engine = test::OpenEngine(dir, DurabilityClass::kSync);
    ASSERT_TRUE(engine);
    ASSERT_TRUE(engine->Put("tf1", "tv").ok());
    ASSERT_TRUE(engine->Checkpoint().ok());
    ASSERT_TRUE(engine->CorruptWalForTest().ok());
  }

  RarReport report{};
  ASSERT_TRUE(BuildPhysicalOnly(dir, &report).ok());
  ASSERT_EQ(report.physical.shards.size(), 1u);
  EXPECT_TRUE(report.physical.shards[0].wal.badwal_marker);
  EXPECT_GE(report.physical.shards[0].reconstructed_key_count, 1u);
}

TEST(RarPhysical, FastOpenDirHasWalRecords) {
  const std::string dir = test::TempDir("rar_phys_fast");
  {
    auto engine = test::OpenEngine(dir, DurabilityClass::kBalanced);
    ASSERT_TRUE(engine);
    ASSERT_TRUE(engine->Put("fo_k", "fo_v").ok());
  }

  PhysicalReport physical{};
  ASSERT_TRUE(PhysicalAttest(dir, 1, &physical).ok());
  EXPECT_GT(physical.shards[0].wal.record_count, 0u);
  EXPECT_GT(physical.shards[0].wal.max_lsn, 0u);
}

}  // namespace
}  // namespace audit
}  // namespace ebtree
