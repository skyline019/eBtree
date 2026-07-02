#include <gtest/gtest.h>

#include "bench/carl_eval_common.h"
#include "engine_test_util.h"

using namespace ebtree;
using namespace ebtree::bench::carl_eval;

TEST(CarlEvalGate, NoCarlVsMonitorRatio) {
  const std::string base = test::TempDir("carl_eval_gate");
  const Write100kResult no_carl = RunNoCarlWrite100k(base + "/no");
  const Write100kResult carl = RunCarlMonitorWrite100k(base + "/carl");
  ASSERT_GT(no_carl.ops_per_sec, 0.0);
  ASSERT_GT(carl.ops_per_sec, 0.0);
#if defined(NDEBUG)
  EXPECT_GE(carl.ops_per_sec, no_carl.ops_per_sec * 0.99);
#endif
}

TEST(CarlEvalGate, VerifyAndAnchorLatencySmoke) {
  const std::string base = test::TempDir("carl_eval_latency");
  const VerifyLatencyResult verify = MeasureChainVerifyLatency(
      base + "/chain.jsonl", 1000);
  ASSERT_EQ(verify.entry_count, 1000u);
  EXPECT_GT(verify.verify_ms, 0);
#if defined(NDEBUG)
  EXPECT_LT(verify.verify_ms, 5000);
#endif

  const AnchorLatencyResult anchor = MeasureAnchorPublishLatency(
      base + "/anchor_chain.jsonl", base + "/anchors");
  EXPECT_GT(anchor.publish_ms, 0);
#if defined(NDEBUG)
  EXPECT_LT(anchor.publish_ms, 500);
#endif
}

#ifndef NDEBUG
TEST(CarlEvalGate, WriteRatioReleaseOnly) {
  GTEST_SKIP() << "100k write ratio gate runs in Release only";
}
#endif
