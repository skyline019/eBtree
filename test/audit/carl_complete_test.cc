#include <gtest/gtest.h>

#include <chrono>
#include <fstream>
#include <thread>

#ifdef _WIN32
#include <stdlib.h>
#else
#include <cstdlib>
#endif

#include "digest.h"
#include "engine_test_util.h"
#include "rar_chain.h"
#include "rar_chain_anchor.h"
#include "rar_chain_worker.h"
#include "rar_merkle.h"
#include "rar_snapshot_builder.h"

#include "ebtree/engine/engine.h"
#include "ebtree/engine/engine_attest.h"

using namespace ebtree;
using namespace ebtree::audit;

TEST(CarlAnchorTamperDetect, RewriteChainFailsAnchorVerify) {
  const std::string dir = test::TempDir("carl_anchor_tamper");
  const std::string chain_path = dir + "/ebtree.rar.chain.jsonl";
  const std::string anchor_dir = dir + "/anchors";

  AttestExportReportV2 kernel{};
  kernel.checkpoint_lsn = 1;
  const std::string body =
      BuildChainBodyJson(1, 1, "", "ophash", 1000, kernel);
  RarChainEntry entry{};
  entry.sequence = 1;
  entry.checkpoint_lsn = 1;
  entry.body_json = body;
  entry.rar_sha256 = Sha256HexString(body);
  ASSERT_TRUE(AppendRarChainEntry(chain_path, entry).ok());

  CarlSignedTreeHead sth{};
  ASSERT_TRUE(PublishCarlAnchor(chain_path, anchor_dir, &sth).ok());
  EXPECT_EQ(sth.root_hash, entry.rar_sha256);

  const std::string tampered_body =
      BuildChainBodyJson(1, 99, "", "ophash", 1000, kernel);
  RarChainEntry bad = entry;
  bad.body_json = tampered_body;
  bad.rar_sha256 = Sha256HexString(tampered_body);
  std::filesystem::remove(chain_path);
  ASSERT_TRUE(AppendRarChainEntry(chain_path, bad).ok());

  std::string reason;
  EXPECT_FALSE(
      VerifyCarlAnchorAgainstChain(chain_path, sth, &reason).ok());
  EXPECT_FALSE(reason.empty());
  EXPECT_FALSE(VerifyCarlAnchorRequired(chain_path, anchor_dir).ok());
}

TEST(CarlAnchorTamperDetect, PublishAndLoadRoundtrip) {
  const std::string dir = test::TempDir("carl_anchor_roundtrip");
  const std::string chain_path = dir + "/chain.jsonl";
  const std::string anchor_dir = dir + "/anchors";

  AttestExportReportV2 kernel{};
  const std::string body = BuildChainBodyJson(1, 1, "", "", 1, kernel);
  RarChainEntry entry{};
  entry.sequence = 1;
  entry.body_json = body;
  entry.rar_sha256 = Sha256HexString(body);
  ASSERT_TRUE(AppendRarChainEntry(chain_path, entry).ok());

  CarlSignedTreeHead published{};
  ASSERT_TRUE(PublishCarlAnchor(chain_path, anchor_dir, &published).ok());

  CarlSignedTreeHead loaded{};
  bool found = false;
  ASSERT_TRUE(
      LoadLatestCarlAnchor(anchor_dir, "chain.jsonl", &loaded, &found).ok());
  ASSERT_TRUE(found);
  EXPECT_EQ(loaded.chain_sequence, 1u);
  EXPECT_EQ(loaded.root_hash, entry.rar_sha256);
  EXPECT_TRUE(VerifyCarlAnchorRequired(chain_path, anchor_dir).ok());
}

TEST(CarlMerkleInclusionProof, WorkerBatchProofVerifies) {
  const std::string dir = test::TempDir("carl_merkle_proof");
  const std::string chain_path = dir + "/ebtree.rar.chain.jsonl";
  const std::string merkle_path = CarlMerkleSidecarPath(chain_path);

  EngineOptions opts = EngineOptions::ProductionDefaults(dir);
  std::unique_ptr<Engine> engine;
  ASSERT_TRUE(Engine::Open(opts, &engine).ok());

  RarChainWorker worker;
  RarChainWorkerOptions worker_opts{};
  worker_opts.chain_path = chain_path;
  worker_opts.enabled = true;
  worker.Start(worker_opts);
  InstallRarChainWorker(engine.get(), &worker);

  for (int i = 0; i < 10; ++i) {
    ASSERT_TRUE(engine->Put("mk" + std::to_string(i), "mv").ok());
    ASSERT_TRUE(engine->Checkpoint().ok());
  }

  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
  std::vector<CarlMerkleBatch> batches;
  while (std::chrono::steady_clock::now() < deadline) {
    batches.clear();
    if (LoadCarlMerkleBatches(merkle_path, &batches).ok() && !batches.empty()) {
      break;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
  }
  worker.Stop();
  ASSERT_FALSE(batches.empty());

  const uint64_t target_seq = batches.front().end_sequence;
  std::vector<RarChainEntry> entries;
  ASSERT_TRUE(ReadRarChainEntries(chain_path, &entries).ok());
  std::string leaf_hash;
  for (const auto& entry : entries) {
    if (entry.sequence == target_seq) {
      CarlMerkleAccumulator acc(1);
      leaf_hash = CarlMerkleHashLeaf(entry.rar_sha256);
      break;
    }
  }
  ASSERT_FALSE(leaf_hash.empty());

  std::vector<std::string> proof;
  std::string root;
  ASSERT_TRUE(
      GenerateCarlMerkleProof(merkle_path, target_seq, &proof, &root).ok());
  EXPECT_EQ(root, batches.front().root_hash);
  EXPECT_FALSE(proof.empty());
  EXPECT_TRUE(VerifyCarlMerkleInclusion(leaf_hash, proof, root).ok());
}

TEST(CarlWorkerAutoPublish, PublishesOnMerkleBatchFlush) {
  const std::string dir = test::TempDir("carl_worker_auto_publish");
  const std::string chain_path = dir + "/ebtree.rar.chain.jsonl";
  const std::string anchor_dir = dir + "/carl_anchors";
  std::filesystem::create_directories(anchor_dir);
#ifdef _WIN32
  _putenv_s("EBTREE_CARL_ANCHOR_PATH", anchor_dir.c_str());
#else
  setenv("EBTREE_CARL_ANCHOR_PATH", anchor_dir.c_str(), 1);
#endif

  EngineOptions opts = EngineOptions::ProductionDefaults(dir);
  std::unique_ptr<Engine> engine;
  ASSERT_TRUE(Engine::Open(opts, &engine).ok());

  RarChainWorker worker;
  RarChainWorkerOptions worker_opts{};
  worker_opts.chain_path = chain_path;
  worker_opts.rotate_max_entries = 10000;
  worker.Start(worker_opts);
  InstallRarChainWorker(engine.get(), &worker);

  for (int i = 0; i < 16; ++i) {
    ASSERT_TRUE(engine->Put("apk" + std::to_string(i), "v").ok());
    ASSERT_TRUE(engine->Checkpoint().ok());
  }

  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
  bool anchor_found = false;
  while (std::chrono::steady_clock::now() < deadline) {
    CarlSignedTreeHead sth{};
    bool found = false;
    if (LoadLatestCarlAnchor(anchor_dir, "ebtree.rar.chain.jsonl", &sth, &found)
            .ok() &&
        found) {
      anchor_found = true;
      break;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
  }
  worker.Stop();
  ASSERT_TRUE(anchor_found);
}

#ifdef EBTREE_RAR_SIGNING
TEST(CarlAnchorSignedRoundtrip, PublishVerifySignature) {
  const char* key_hex =
      "000102030405060708090a0b0c0d0e0f"
      "000102030405060708090a0b0c0d0e0f";
#ifdef _WIN32
  _putenv_s("EBTREE_RAR_KEY", key_hex);
#else
  setenv("EBTREE_RAR_KEY", key_hex, 1);
#endif
  const std::string dir = test::TempDir("carl_anchor_signed");
  const std::string chain_path = dir + "/chain.jsonl";
  const std::string anchor_dir = dir + "/anchors";

  AttestExportReportV2 kernel{};
  const std::string body = BuildChainBodyJson(1, 1, "", "", 1, kernel);
  RarChainEntry entry{};
  entry.body_json = body;
  entry.rar_sha256 = Sha256HexString(body);
  entry.sequence = 1;
  ASSERT_TRUE(AppendRarChainEntry(chain_path, entry).ok());

  CarlSignedTreeHead sth{};
  ASSERT_TRUE(PublishCarlAnchor(chain_path, anchor_dir, &sth).ok());
  ASSERT_FALSE(sth.signature.empty());
  EXPECT_TRUE(VerifyCarlAnchorRequired(chain_path, anchor_dir, true).ok());
}

TEST(CarlAnchorSignatureTamperDetect, BadSignatureFails) {
  const char* key_hex =
      "000102030405060708090a0b0c0d0e0f"
      "000102030405060708090a0b0c0d0e0f";
#ifdef _WIN32
  _putenv_s("EBTREE_RAR_KEY", key_hex);
#else
  setenv("EBTREE_RAR_KEY", key_hex, 1);
#endif
  const std::string dir = test::TempDir("carl_anchor_sig_tamper");
  const std::string chain_path = dir + "/chain.jsonl";
  const std::string anchor_dir = dir + "/anchors";

  AttestExportReportV2 kernel{};
  const std::string body = BuildChainBodyJson(1, 1, "", "", 1, kernel);
  RarChainEntry entry{};
  entry.body_json = body;
  entry.rar_sha256 = Sha256HexString(body);
  entry.sequence = 1;
  ASSERT_TRUE(AppendRarChainEntry(chain_path, entry).ok());

  CarlSignedTreeHead sth{};
  ASSERT_TRUE(PublishCarlAnchor(chain_path, anchor_dir, &sth).ok());
  sth.signature = "tampered";
  std::string reason;
  EXPECT_FALSE(VerifyCarlAnchorSignature(sth, key_hex).ok());
}
#endif
