#include <gtest/gtest.h>

#include "engine_test_util.h"

class EbNestedMemTableFixture : public ::testing::Test {
 protected:
  void SetUp() override {
    dir_ = ebtree::test::TempDir("nested_memtable");
    engine_ = ebtree::test::OpenEngine(dir_);
    ASSERT_NE(engine_, nullptr);
  }

  std::string dir_;
  std::unique_ptr<ebtree::Engine> engine_;
};

TEST_F(EbNestedMemTableFixture, NestedPutVisibleBeforeFlush) {
  ASSERT_TRUE(engine_->Put("n1", "v1").ok());
  std::string value;
  ASSERT_TRUE(engine_->Get("n1", &value).ok());
  EXPECT_EQ(value, "v1");
}

TEST_F(EbNestedMemTableFixture, NestedFlushPersists) {
  ASSERT_TRUE(engine_->Put("n2", "v2").ok());
  ASSERT_TRUE(engine_->Flush().ok());
  std::string value;
  ASSERT_TRUE(engine_->Get("n2", &value).ok());
  EXPECT_EQ(value, "v2");
}

TEST_F(EbNestedMemTableFixture, NestedRotateThenFlushClearsFrozen) {
  ASSERT_TRUE(engine_->Put("r1", "before").ok());
  ASSERT_TRUE(engine_->Flush().ok());
  ASSERT_TRUE(engine_->Put("r2", "after").ok());
  std::string value;
  ASSERT_TRUE(engine_->Get("r1", &value).ok());
  EXPECT_EQ(value, "before");
  ASSERT_TRUE(engine_->Get("r2", &value).ok());
  EXPECT_EQ(value, "after");
  ASSERT_TRUE(engine_->Flush().ok());
  ASSERT_TRUE(engine_->Get("r2", &value).ok());
  EXPECT_EQ(value, "after");
}
