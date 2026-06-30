#include <gtest/gtest.h>

#include "ebtree/common/status.h"
#include "ebtree/engine/engine.h"
#include "engine_test_util.h"

TEST(EbFailure, EmptyKeyPutRejected) {
  const std::string dir = ebtree::test::TempDir("failure_empty_key");
  auto engine = ebtree::test::OpenEngine(dir);
  ASSERT_NE(engine, nullptr);
  const ebtree::Status st = engine->Put("", "v");
  EXPECT_EQ(st.code(), ebtree::StatusCode::kInvalidArgument);
}

TEST(EbFailure, CorruptSuperBlockRejected) {
  const std::string dir = ebtree::test::TempDir("failure_corrupt_sb");
  {
    auto engine = ebtree::test::OpenEngine(dir);
    ASSERT_NE(engine, nullptr);
    ASSERT_TRUE(engine->Put("k", "v").ok());
    ASSERT_TRUE(engine->Checkpoint().ok());
    ASSERT_TRUE(engine->CorruptSuperBlockForTest().ok());
  }
  ebtree::EngineOptions opts = ebtree::EngineOptions::EnterpriseDefaults(dir);
  std::unique_ptr<ebtree::Engine> reopened;
  const ebtree::Status st = ebtree::Engine::Open(opts, &reopened);
  EXPECT_EQ(st.code(), ebtree::StatusCode::kCorrupt);
  EXPECT_NE(st.message().find("CorruptSuperBlock"), std::string::npos);
}
