#include <fstream>
#include <gtest/gtest.h>

#include <set>
#include <sstream>
#include <string>
#include <vector>

#ifndef EBTEST_SYNC_MANIFEST
#define EBTEST_SYNC_MANIFEST "Docs/syncs/SYNC_MANIFEST.yaml"
#endif

namespace {

std::vector<std::string> ParseRuleNames(const std::string& yaml) {
  std::vector<std::string> names;
  std::istringstream in(yaml);
  std::string line;
  while (std::getline(in, line)) {
    const auto pos = line.find("- name:");
    if (pos == std::string::npos) continue;
    std::string name = line.substr(pos + 7);
    while (!name.empty() && (name.front() == ' ' || name.front() == '\t')) {
      name.erase(name.begin());
    }
    if (!name.empty()) names.push_back(name);
  }
  return names;
}

}  // namespace

TEST(EbSyncManifest, NoStubHandlers) {
  std::ifstream in(EBTEST_SYNC_MANIFEST);
  ASSERT_TRUE(in.good()) << "SYNC_MANIFEST.yaml not found at "
                         << EBTEST_SYNC_MANIFEST;
  std::stringstream buffer;
  buffer << in.rdbuf();
  const std::string content = buffer.str();
  EXPECT_EQ(content.find("handler: stub"), std::string::npos);
}

TEST(EbSyncManifest, RequiredRuleNamesPresent) {
  std::ifstream in(EBTEST_SYNC_MANIFEST);
  ASSERT_TRUE(in.good());
  std::stringstream buffer;
  buffer << in.rdbuf();
  const auto names = ParseRuleNames(buffer.str());
  const std::set<std::string> required = {
      "WriteMemTableSync",   "GroupCommitSync",      "FlusherSync",
      "FlushApplySync",      "SuperBlockCommitSync", "CrashRecoverySync",
      "TLogSnapshotSync",    "LazyRecoverySync",     "SummaryHealSync",
      "GcRegionSwapSync",    "MemTablePipelineSync", "FlashbackReadSync",
      "MmapReadSync",        "ShardRouteSync",       "BackgroundFlushSync",
      "PagedBTreeSync",      "ConcurrentReadSync",   "PlanValidateSync",
      "WalAppendSync",
  };
  std::set<std::string> found(names.begin(), names.end());
  for (const auto& name : required) {
    EXPECT_TRUE(found.count(name) > 0) << name;
  }
}
