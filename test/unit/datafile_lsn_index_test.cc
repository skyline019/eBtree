#include <gtest/gtest.h>

#include <filesystem>

#include "ebtree/concept/datafile/datafile.h"
#include "ebtree/concept/datafile/datafile_lsn_index.h"
#include "engine_test_util.h"

TEST(EbDataFileLsnIndex, UpdateLookup) {
  ebtree::DataFileLsnIndex idx;
  idx.Update(100, 5);
  idx.Update(200, 10);
  uint64_t off = 0;
  EXPECT_TRUE(idx.Lookup(5, &off));
  EXPECT_EQ(off, 100u);
  EXPECT_TRUE(idx.Lookup(10, &off));
  EXPECT_EQ(off, 200u);
  EXPECT_FALSE(idx.Lookup(99, &off));
}

TEST(EbDataFileLsnIndex, BuildFromFile) {
  const std::string dir = ebtree::test::TempDir("didx_build");
  ebtree::DataFile df(dir + "/shard0.data");
  ASSERT_TRUE(df.Append(1, "k1", "v1", false).ok());
  ASSERT_TRUE(df.Append(2, "k2", "v2", false).ok());
  df.FlushAppendStream();

  ebtree::DataFileLsnIndex idx;
  ASSERT_TRUE(idx.BuildFromFile(df.path()).ok());
  uint64_t off = 0;
  ASSERT_TRUE(idx.Lookup(1, &off));
  EXPECT_EQ(off, 0u);
  ASSERT_TRUE(idx.Lookup(2, &off));
  EXPECT_GT(off, 0u);
}
