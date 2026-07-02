#include <gtest/gtest.h>

#include "ebtree/engine/sfs_read_cache.h"

namespace ebtree {
namespace {

TEST(SfsRead, InsertLookupAndTombstone) {
  SfsReadCache cache(4);
  EXPECT_FALSE(cache.Lookup(42, 100).has_value());
  cache.Insert(42, 100, 999, false);
  ASSERT_TRUE(cache.Lookup(42, 100).has_value());
  EXPECT_EQ(*cache.Lookup(42, 100), 999u);
  cache.Insert(42, 100, 0, true);
  ASSERT_TRUE(cache.Lookup(42, 100).has_value());
  EXPECT_EQ(*cache.Lookup(42, 100), 0u);
}

TEST(SfsRead, EpochQuantization) {
  EXPECT_EQ(SfsEpoch(150, 100), 100u);
  EXPECT_EQ(SfsEpoch(199, 100), 100u);
}

}  // namespace
}  // namespace ebtree
