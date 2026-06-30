#include <gtest/gtest.h>

#include "engine_test_util.h"

TEST(EbComplex, ThousandPutCheckpointReopen) {
  const std::string dir = ebtree::test::TempDir("complex_1k");
  {
    auto engine = ebtree::test::OpenEngine(dir);
    ASSERT_NE(engine, nullptr);
    for (int i = 0; i < 1000; ++i) {
      const std::string key = "k" + std::to_string(i);
      const std::string val = "v" + std::to_string(i);
      ASSERT_TRUE(engine->Put(key, val).ok());
    }
    ASSERT_TRUE(engine->Checkpoint().ok());
  }
  auto engine = ebtree::test::OpenEngine(dir);
  ASSERT_NE(engine, nullptr);
  for (int i = 0; i < 1000; i += 137) {
    std::string value;
    const std::string key = "k" + std::to_string(i);
    ASSERT_TRUE(engine->Get(key, &value).ok());
    EXPECT_EQ(value, "v" + std::to_string(i));
  }
}
