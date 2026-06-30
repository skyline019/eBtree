#include <fstream>
#include <gtest/gtest.h>

#include <algorithm>
#include <filesystem>
#include <unordered_map>
#include <vector>

#include "ebtree/concept/datafile/datafile.h"
#include "ebtree/concept/superblock/superblock.h"
#include "engine_test_util.h"

namespace {

std::uintmax_t DataFileSize(const std::string& dir) {
  const std::filesystem::path p = dir + "/shard0.data";
  if (!std::filesystem::exists(p)) return 0;
  return std::filesystem::file_size(p);
}

std::vector<uint64_t> ReadDataFileLsns(const std::string& dir) {
  std::vector<uint64_t> lsns;
  ebtree::DataFile df(dir + "/shard0.data");
  std::unordered_map<std::string, std::pair<std::string, uint64_t>> data;
  uint64_t max_lsn = 0;
  (void)df.LoadAll(&data, &max_lsn);
  lsns.reserve(data.size());
  for (const auto& kv : data) {
    lsns.push_back(kv.second.second);
  }
  std::sort(lsns.begin(), lsns.end());
  return lsns;
}

}  // namespace

TEST(EbInvariantDurability, WalPrecedesDataFile) {
  const std::string dir = ebtree::test::TempDir("inv_d1");
  auto engine = ebtree::test::OpenEngine(dir);
  ASSERT_NE(engine, nullptr);
  const auto size_before = DataFileSize(dir);
  ASSERT_TRUE(engine->Put("d1k", "d1v").ok());
  EXPECT_EQ(DataFileSize(dir), size_before);
  EXPECT_GE(engine->stats().wal_append_total, 1u);
  ASSERT_TRUE(engine->Flush().ok());
  EXPECT_GT(DataFileSize(dir), size_before);
  EXPECT_TRUE(engine->committed().count("d1k") > 0);
}

TEST(EbInvariantDurability, SuperBlockDataLsnLeStable) {
  const std::string dir = ebtree::test::TempDir("inv_d2");
  auto engine = ebtree::test::OpenEngine(dir);
  ASSERT_NE(engine, nullptr);
  ASSERT_TRUE(engine->Put("d2k", "d2v").ok());
  ASSERT_TRUE(engine->Checkpoint().ok());
  ebtree::SuperBlock sb{};
  ebtree::SuperBlockStore store(dir + "/shard0.super");
  ASSERT_TRUE(store.Load(&sb).ok());
  EXPECT_LE(sb.critical.data_lsn, engine->stable_lsn());
}

TEST(EbInvariantDurability, DataFileLsnMonotonic) {
  const std::string dir = ebtree::test::TempDir("inv_d3");
  auto engine = ebtree::test::OpenEngine(dir);
  ASSERT_NE(engine, nullptr);
  ASSERT_TRUE(engine->Put("a", "1").ok());
  ASSERT_TRUE(engine->Flush().ok());
  ASSERT_TRUE(engine->Put("b", "2").ok());
  ASSERT_TRUE(engine->Flush().ok());
  const auto lsns = ReadDataFileLsns(dir);
  ASSERT_GE(lsns.size(), 2u);
  for (size_t i = 1; i < lsns.size(); ++i) {
    EXPECT_LE(lsns[i - 1], lsns[i]);
  }
}

TEST(EbInvariantNoFallback, EmptyKeyGetRejected) {
  const std::string dir = ebtree::test::TempDir("inv_nf0");
  auto engine = ebtree::test::OpenEngine(dir);
  ASSERT_NE(engine, nullptr);
  std::string value;
  const ebtree::Status st = engine->Get("", &value);
  EXPECT_FALSE(st.ok());
  EXPECT_EQ(st.code(), ebtree::StatusCode::kInvalidArgument);
}

TEST(EbInvariantNoFallback, ReadPathNoRecovery) {
  const std::string dir = ebtree::test::TempDir("inv_nf3");
  auto engine = ebtree::test::OpenEngine(dir);
  ASSERT_NE(engine, nullptr);
  const uint64_t before = engine->stats().recovery_total;
  ASSERT_TRUE(engine->Put("nf3", "v").ok());
  std::string value;
  ASSERT_TRUE(engine->Get("nf3", &value).ok());
  ebtree::TypedPlan plan;
  plan.op = ebtree::PredicateOp::kEq;
  plan.key = "nf3";
  plan.snapshot_lsn = engine->stable_lsn();
  std::vector<std::pair<std::string, std::string>> rows;
  ASSERT_TRUE(engine->Scan(plan, &rows).ok());
  EXPECT_EQ(engine->stats().recovery_total, before);
}
