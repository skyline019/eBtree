#include <gtest/gtest.h>

#include "ebtree/concept/wal/wal.h"
#include "ebtree/concept/wal/wal_key_index.h"
#include "engine_test_util.h"

TEST(EbWalKeyIndex, BuildAndLookup) {
  const std::string dir = ebtree::test::TempDir("wal_index");
  const std::string path = dir + "/shard0.wal";
  ebtree::WalWriter wal(path);
  uint64_t lsn = 0;
  ASSERT_TRUE(wal.Append(ebtree::WalOp::kPut, "alpha", "one", &lsn).ok());
  ASSERT_TRUE(wal.Append(ebtree::WalOp::kPut, "beta", "two", &lsn).ok());
  ASSERT_TRUE(wal.Append(ebtree::WalOp::kPut, "alpha", "three", &lsn).ok());
  ASSERT_TRUE(wal.Fsync().ok());
  uint64_t offset = 0;
  EXPECT_TRUE(wal.key_index().Lookup("alpha", 0, &offset));
  uint64_t out_lsn = 0;
  ASSERT_TRUE(wal.ReplayKey(0, "alpha", &out_lsn).ok());
  EXPECT_EQ(3u, out_lsn);
}

TEST(EbWalKeyIndex, StandaloneBuildFromFile) {
  const std::string dir = ebtree::test::TempDir("wal_index_build");
  const std::string path = dir + "/shard0.wal";
  {
    ebtree::WalWriter wal(path);
    uint64_t lsn = 0;
    ASSERT_TRUE(wal.Append(ebtree::WalOp::kPut, "k1", "v1", &lsn).ok());
  }
  ebtree::WalKeyIndex index;
  ASSERT_TRUE(index.BuildFromFile(path).ok());
  uint64_t offset = 0;
  EXPECT_TRUE(index.Lookup("k1", 0, &offset));
}
