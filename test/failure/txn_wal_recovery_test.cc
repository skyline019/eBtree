#include <gtest/gtest.h>

#include "engine_test_util.h"
#include "ebtree/engine/engine.h"

namespace ebtree {
namespace test {

TEST(TxnWalRecovery, CommittedVisibleAfterReopen) {
  const std::string dir = TempDir("txn_wal_commit");
  EngineOptions opts = EngineOptions::ProductionDefaults(dir);
  opts.durability = DurabilityClass::kSync;
  std::unique_ptr<Engine> engine;
  ASSERT_TRUE(Engine::Open(opts, &engine).ok());
  ASSERT_TRUE(engine->AppendTxnBegin(7, engine->CaptureSnapshot()).ok());
  ASSERT_TRUE(engine->Put("k", "v", 7).ok());
  ASSERT_TRUE(engine->AppendTxnCommit(7).ok());
  engine->memtable()->PromoteTxn(7);
  ASSERT_TRUE(engine->Checkpoint().ok());
  engine.reset();

  ASSERT_TRUE(Engine::Open(opts, &engine).ok());
  std::string value;
  ASSERT_TRUE(engine->Get("k", &value).ok());
  EXPECT_EQ(value, "v");
}

TEST(TxnWalRecovery, UncommittedHiddenAfterReopen) {
  const std::string dir = TempDir("txn_wal_open");
  EngineOptions opts = EngineOptions::ProductionDefaults(dir);
  opts.durability = DurabilityClass::kSync;
  std::unique_ptr<Engine> engine;
  ASSERT_TRUE(Engine::Open(opts, &engine).ok());
  ASSERT_TRUE(engine->AppendTxnBegin(9, engine->CaptureSnapshot()).ok());
  ASSERT_TRUE(engine->Put("open_k", "dirty", 9).ok());
  engine.reset();

  ASSERT_TRUE(Engine::Open(opts, &engine).ok());
  std::string value;
  const Status st = engine->Get("open_k", &value);
  EXPECT_FALSE(st.ok());
}

}  // namespace test
}  // namespace ebtree
