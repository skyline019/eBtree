#include <gtest/gtest.h>

#include <chrono>

#include "engine_test_util.h"
#include "rar_chain.h"
#include "rar_chain_anchor.h"
#include "rar_snapshot_builder.h"
#include "digest.h"

#include "ebtree/engine/engine_attest.h"

using namespace ebtree;
using namespace ebtree::audit;

TEST(CarlBenchmark, ChainVerifyLatency1k) {
  const std::string chain_path =
      test::TempDir("carl_bench_verify") + "/chain.jsonl";
  AttestExportReportV2 kernel{};
  kernel.checkpoint_lsn = 1;
  std::string prev;
  for (uint64_t seq = 1; seq <= 1000; ++seq) {
    const std::string body =
        BuildChainBodyJson(seq, seq, prev, "", static_cast<int64_t>(seq), kernel);
    RarChainEntry entry{};
    entry.sequence = seq;
    entry.body_json = body;
    entry.rar_sha256 = Sha256HexString(body);
    entry.prev_rar_sha256 = prev;
    ASSERT_TRUE(AppendRarChainEntry(chain_path, entry).ok());
    prev = entry.rar_sha256;
  }
  const auto start = std::chrono::steady_clock::now();
  RarChainVerifyReport report{};
  ASSERT_TRUE(VerifyRarChain(chain_path, &report).ok());
  const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
      std::chrono::steady_clock::now() - start);
  EXPECT_TRUE(report.consistent);
  EXPECT_EQ(report.entry_count, 1000u);
#if defined(NDEBUG)
  EXPECT_LT(ms.count(), 5000);
#endif
}

TEST(CarlBenchmark, AnchorPublishLatency) {
  const std::string dir = test::TempDir("carl_bench_anchor");
  const std::string chain_path = dir + "/chain.jsonl";
  const std::string anchor_dir = dir + "/anchors";
  AttestExportReportV2 kernel{};
  const std::string body = BuildChainBodyJson(1, 1, "", "", 1, kernel);
  RarChainEntry entry{};
  entry.body_json = body;
  entry.rar_sha256 = Sha256HexString(body);
  entry.sequence = 1;
  ASSERT_TRUE(AppendRarChainEntry(chain_path, entry).ok());

  const auto start = std::chrono::steady_clock::now();
  CarlSignedTreeHead sth{};
  ASSERT_TRUE(PublishCarlAnchor(chain_path, anchor_dir, &sth).ok());
  const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
      std::chrono::steady_clock::now() - start);
#if defined(NDEBUG)
  EXPECT_LT(ms.count(), 100);
#endif
}
