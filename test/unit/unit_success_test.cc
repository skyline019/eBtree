#include <gtest/gtest.h>

#include "engine_test_util.h"

TEST(EbUnitSuccess, WalAppendCounts) {
  const std::string dir = ebtree::test::TempDir("unit_wal_success");
  auto engine = ebtree::test::OpenEngine(dir);
  ASSERT_NE(engine, nullptr);
  ASSERT_TRUE(engine->Put("k1", "v1").ok());
  EXPECT_GE(engine->stats().wal_append_total, 1u);
}

TEST(EbUnitSuccess, PutGetRoundtrip) {
  const std::string dir = ebtree::test::TempDir("unit_put_get");
  auto engine = ebtree::test::OpenEngine(dir);
  ASSERT_NE(engine, nullptr);
  ASSERT_TRUE(engine->Put("alpha", "beta").ok());
  ASSERT_TRUE(engine->Checkpoint().ok());
  std::string value;
  ASSERT_TRUE(engine->Get("alpha", &value).ok());
  EXPECT_EQ(value, "beta");
}
