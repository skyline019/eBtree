#include <gtest/gtest.h>

#include <unordered_map>

#include "ebtree/concept/datafile/datafile.h"
#include "ebtree/concept/mmap/mmap_window.h"
#include "engine_test_util.h"

TEST(EbInvariantMmap, LoadAllEquivalentToMmapView) {
  const std::string dir = ebtree::test::TempDir("mmap_equiv");
  ebtree::DataFile df(dir + "/shard0.data");
  ASSERT_TRUE(df.Append(1, "a", "va", false).ok());
  ASSERT_TRUE(df.Append(2, "b", "vb", false).ok());

  std::unordered_map<std::string, std::pair<std::string, uint64_t>> stream_map;
  uint64_t stream_max = 0;
  ASSERT_TRUE(
      df.LoadAll(&stream_map, &stream_max, ebtree::kDataFileGenerationAll).ok());

  ebtree::MmapWindowManager mgr;
  ASSERT_TRUE(mgr.OpenReadOnly(df.path()).ok());
  ebtree::MmapView view{};
  ASSERT_TRUE(mgr.Pin(&view).ok());
  std::unordered_map<std::string, std::pair<std::string, uint64_t>> mmap_map;
  uint64_t mmap_max = 0;
  ASSERT_TRUE(df.LoadRecordsFromView(view.base, view.size, &mmap_map, &mmap_max)
                  .ok());
  mgr.Unpin();

  EXPECT_EQ(stream_map, mmap_map);
  EXPECT_EQ(stream_max, mmap_max);
}

TEST(EbInvariantMmap, RotateEpochOldPinStillReadable) {
  const std::string dir = ebtree::test::TempDir("mmap_epoch");
  ebtree::DataFile df(dir + "/shard0.data");
  ASSERT_TRUE(df.Append(1, "e1", "v1", false).ok());
  df.FlushAppendStream();

  ebtree::MmapWindowManager mgr;
  ASSERT_TRUE(mgr.OpenReadOnly(df.path()).ok());
  ebtree::MmapView v1{};
  ASSERT_TRUE(mgr.Pin(&v1).ok());
  ASSERT_TRUE(df.Append(2, "e2", "v2", false).ok());
  df.FlushAppendStream();
  ASSERT_TRUE(mgr.RotateEpoch().ok());
  ebtree::MmapView v2{};
  ASSERT_TRUE(mgr.Pin(&v2).ok());

  std::unordered_map<std::string, std::pair<std::string, uint64_t>> old_map;
  uint64_t old_max = 0;
  ASSERT_TRUE(
      df.LoadRecordsFromView(v1.base, v1.size, &old_map, &old_max).ok());
  EXPECT_EQ(old_map.size(), 1u);
  EXPECT_EQ(old_map["e1"].first, "v1");

  std::unordered_map<std::string, std::pair<std::string, uint64_t>> new_map;
  uint64_t new_max = 0;
  ASSERT_TRUE(
      df.LoadRecordsFromView(v2.base, v2.size, &new_map, &new_max).ok());
  EXPECT_EQ(new_map.size(), 2u);

  mgr.Unpin();
  mgr.Unpin();
}
