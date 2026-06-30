#include <gtest/gtest.h>

#include "chaos_runner.h"
#include "engine_test_util.h"

namespace {

int ChaosThreadCount() {
#if defined(EBTEST_CI)
  return 128;
#else
  return 1000;
#endif
}

int ChaosWriterOps() {
#if defined(EBTEST_CI)
  return 200;
#else
  return 500;
#endif
}

}  // namespace

TEST(EbFailureConcurrentChaos, ManyReadersDuringWrites) {
  const std::string dir = ebtree::test::TempDir("conc_chaos");
  auto engine = ebtree::test::OpenEngine(dir);
  ASSERT_NE(engine, nullptr);
  const int errors =
      ebtree::test::ConcurrentReadWriteChaos(engine.get(), 77,
                                             ChaosThreadCount(),
                                             ChaosWriterOps());
  EXPECT_EQ(errors, 0);
  EXPECT_EQ(engine->stats().fallback_read_total, 0u);
}

TEST(EbFailureConcurrentChaos, BackgroundFlushWithReaders) {
  const std::string dir = ebtree::test::TempDir("conc_chaos_bg");
  ebtree::EngineOptions opts;
  opts.path = dir;
  opts.durability = ebtree::DurabilityClass::kSync;
  opts.sync_on_commit = true;
  opts.background_flush = true;
  opts.memtable_flush_threshold_keys = 16;
  std::unique_ptr<ebtree::Engine> engine;
  ASSERT_TRUE(ebtree::Engine::Open(opts, &engine).ok());
  const int errors =
      ebtree::test::ConcurrentReadWriteChaos(engine.get(), 88, 32, 200);
  EXPECT_EQ(errors, 0);
}
