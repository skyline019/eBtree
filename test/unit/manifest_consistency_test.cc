#include <fstream>
#include <gtest/gtest.h>

#include <algorithm>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include "test_suites.inc"

#ifndef EBTEST_TEST_MANIFEST
#define EBTEST_TEST_MANIFEST "test/TEST_MANIFEST.yaml"
#endif

namespace {

std::vector<std::string> ParseGateSuites(const std::string& yaml,
                                         const std::string& gate) {
  const std::string key = gate + ":";
  const auto pos = yaml.find(key);
  if (pos == std::string::npos) return {};
  const auto bracket = yaml.find('[', pos);
  const auto end = yaml.find(']', bracket);
  if (bracket == std::string::npos || end == std::string::npos) return {};
  std::string inner = yaml.substr(bracket + 1, end - bracket - 1);
  std::vector<std::string> out;
  std::stringstream ss(inner);
  std::string item;
  while (std::getline(ss, item, ',')) {
    while (!item.empty() && item.front() == ' ') item.erase(item.begin());
    while (!item.empty() && item.back() == ' ') item.pop_back();
    if (!item.empty()) out.push_back(item);
  }
  return out;
}

}  // namespace

TEST(EbManifestConsistency, SuiteCountMatchesExpected) {
  EXPECT_GE(kEbTestSuiteCount, 7);
  bool seen_unit = false;
  bool seen_matrix = false;
  for (int i = 0; i < kEbTestSuiteCount; ++i) {
    const auto& e = kEbTestSuites[i];
    EXPECT_FALSE(std::string(e.name).empty());
    EXPECT_FALSE(std::string(e.dll_basename).empty());
    if (std::string(e.name) == "unit") seen_unit = true;
    if (std::string(e.name) == "matrix") seen_matrix = true;
  }
  EXPECT_TRUE(seen_unit);
  EXPECT_TRUE(seen_matrix);
}

TEST(EbManifestConsistency, P7CompleteGateReferencesKnownSuites) {
  std::ifstream in(EBTEST_TEST_MANIFEST);
  ASSERT_TRUE(in.good()) << EBTEST_TEST_MANIFEST;
  std::stringstream buffer;
  buffer << in.rdbuf();
  const auto gate_suites = ParseGateSuites(buffer.str(), "P7-complete");
  ASSERT_FALSE(gate_suites.empty());
  std::set<std::string> registered;
  for (int i = 0; i < kEbTestSuiteCount; ++i) {
    registered.insert(kEbTestSuites[i].name);
  }
  for (const auto& suite : gate_suites) {
    const auto slash = suite.find('/');
    const std::string base =
        slash == std::string::npos ? suite : suite.substr(0, slash);
    EXPECT_TRUE(registered.count(base) > 0) << suite;
  }
}

TEST(EbManifestConsistency, P8CompleteGateReferencesKnownSuites) {
  std::ifstream in(EBTEST_TEST_MANIFEST);
  ASSERT_TRUE(in.good()) << EBTEST_TEST_MANIFEST;
  std::stringstream buffer;
  buffer << in.rdbuf();
  const auto gate_suites = ParseGateSuites(buffer.str(), "P8-complete");
  ASSERT_FALSE(gate_suites.empty());
  std::set<std::string> registered;
  for (int i = 0; i < kEbTestSuiteCount; ++i) {
    registered.insert(kEbTestSuites[i].name);
  }
  for (const auto& suite : gate_suites) {
    const auto slash = suite.find('/');
    const std::string base =
        slash == std::string::npos ? suite : suite.substr(0, slash);
    EXPECT_TRUE(registered.count(base) > 0) << suite;
  }
}

TEST(EbManifestConsistency, P9CompleteGateReferencesKnownSuites) {
  std::ifstream in(EBTEST_TEST_MANIFEST);
  ASSERT_TRUE(in.good()) << EBTEST_TEST_MANIFEST;
  std::stringstream buffer;
  buffer << in.rdbuf();
  const auto gate_suites = ParseGateSuites(buffer.str(), "P9-complete");
  ASSERT_FALSE(gate_suites.empty());
  std::set<std::string> registered;
  for (int i = 0; i < kEbTestSuiteCount; ++i) {
    registered.insert(kEbTestSuites[i].name);
  }
  for (const auto& suite : gate_suites) {
    const auto slash = suite.find('/');
    const std::string base =
        slash == std::string::npos ? suite : suite.substr(0, slash);
    EXPECT_TRUE(registered.count(base) > 0) << suite;
  }
}

TEST(EbManifestConsistency, P6CompleteGateReferencesKnownSuites) {
  std::ifstream in(EBTEST_TEST_MANIFEST);
  ASSERT_TRUE(in.good()) << EBTEST_TEST_MANIFEST;
  std::stringstream buffer;
  buffer << in.rdbuf();
  const auto gate_suites = ParseGateSuites(buffer.str(), "P6-complete");
  ASSERT_FALSE(gate_suites.empty());
  std::set<std::string> registered;
  for (int i = 0; i < kEbTestSuiteCount; ++i) {
    registered.insert(kEbTestSuites[i].name);
  }
  for (const auto& suite : gate_suites) {
    const auto slash = suite.find('/');
    const std::string base =
        slash == std::string::npos ? suite : suite.substr(0, slash);
    EXPECT_TRUE(registered.count(base) > 0) << suite;
  }
}

TEST(EbManifestConsistency, P6CompleteGateIncludesSqlAudit) {
  std::ifstream in(EBTEST_TEST_MANIFEST);
  ASSERT_TRUE(in.good()) << EBTEST_TEST_MANIFEST;
  std::stringstream buffer;
  buffer << in.rdbuf();
  const auto gate_suites = ParseGateSuites(buffer.str(), "P6-complete");
  ASSERT_FALSE(gate_suites.empty());
  bool seen_sql = false;
  bool seen_audit = false;
  for (const auto& suite : gate_suites) {
    if (suite == "sql") seen_sql = true;
    if (suite == "audit") seen_audit = true;
  }
  EXPECT_TRUE(seen_sql);
  EXPECT_TRUE(seen_audit);
}

TEST(EbManifestConsistency, P6SqlGateReferencesKnownSuites) {
  std::ifstream in(EBTEST_TEST_MANIFEST);
  ASSERT_TRUE(in.good()) << EBTEST_TEST_MANIFEST;
  std::stringstream buffer;
  buffer << in.rdbuf();
  const auto gate_suites = ParseGateSuites(buffer.str(), "P6-sql");
  ASSERT_FALSE(gate_suites.empty());
  std::set<std::string> registered;
  for (int i = 0; i < kEbTestSuiteCount; ++i) {
    registered.insert(kEbTestSuites[i].name);
  }
  for (const auto& suite : gate_suites) {
    const auto slash = suite.find('/');
    const std::string base =
        slash == std::string::npos ? suite : suite.substr(0, slash);
    EXPECT_TRUE(registered.count(base) > 0) << suite;
  }
}

TEST(EbManifestConsistency, P8ProgramCompleteGateReferencesKnownSuites) {
  std::ifstream in(EBTEST_TEST_MANIFEST);
  ASSERT_TRUE(in.good()) << EBTEST_TEST_MANIFEST;
  std::stringstream buffer;
  buffer << in.rdbuf();
  const auto gate_suites = ParseGateSuites(buffer.str(), "P8-program-complete");
  ASSERT_FALSE(gate_suites.empty());
  std::set<std::string> registered;
  for (int i = 0; i < kEbTestSuiteCount; ++i) {
    registered.insert(kEbTestSuites[i].name);
  }
  for (const auto& suite : gate_suites) {
    const auto slash = suite.find('/');
    const std::string base =
        slash == std::string::npos ? suite : suite.substr(0, slash);
    EXPECT_TRUE(registered.count(base) > 0) << suite;
  }
}

TEST(EbManifestConsistency, P10ProgramHonestGateReferencesKnownSuites) {
  std::ifstream in(EBTEST_TEST_MANIFEST);
  ASSERT_TRUE(in.good()) << EBTEST_TEST_MANIFEST;
  std::stringstream buffer;
  buffer << in.rdbuf();
  const auto gate_suites = ParseGateSuites(buffer.str(), "P10-program-honest");
  ASSERT_FALSE(gate_suites.empty());
  std::set<std::string> registered;
  for (int i = 0; i < kEbTestSuiteCount; ++i) {
    registered.insert(kEbTestSuites[i].name);
  }
  for (const auto& suite : gate_suites) {
    const auto slash = suite.find('/');
    const std::string base =
        slash == std::string::npos ? suite : suite.substr(0, slash);
    EXPECT_TRUE(registered.count(base) > 0) << suite;
  }
}

TEST(EbManifestConsistency, P5CompleteGateReferencesKnownSuites) {
  std::ifstream in(EBTEST_TEST_MANIFEST);
  ASSERT_TRUE(in.good()) << EBTEST_TEST_MANIFEST;
  std::stringstream buffer;
  buffer << in.rdbuf();
  const auto gate_suites = ParseGateSuites(buffer.str(), "P5-complete");
  ASSERT_FALSE(gate_suites.empty());
  std::set<std::string> registered;
  for (int i = 0; i < kEbTestSuiteCount; ++i) {
    registered.insert(kEbTestSuites[i].name);
  }
  for (const auto& suite : gate_suites) {
    const auto slash = suite.find('/');
    const std::string base =
        slash == std::string::npos ? suite : suite.substr(0, slash);
    EXPECT_TRUE(registered.count(base) > 0) << suite;
  }
}

TEST(EbManifestConsistency, P5GateReferencesKnownSuites) {
  std::ifstream in(EBTEST_TEST_MANIFEST);
  ASSERT_TRUE(in.good()) << EBTEST_TEST_MANIFEST;
  std::stringstream buffer;
  buffer << in.rdbuf();
  const auto gate_suites = ParseGateSuites(buffer.str(), "P5");
  ASSERT_FALSE(gate_suites.empty());
  std::set<std::string> registered;
  for (int i = 0; i < kEbTestSuiteCount; ++i) {
    registered.insert(kEbTestSuites[i].name);
  }
  for (const auto& suite : gate_suites) {
    const auto slash = suite.find('/');
    const std::string base =
        slash == std::string::npos ? suite : suite.substr(0, slash);
    EXPECT_TRUE(registered.count(base) > 0) << suite;
  }
}

TEST(EbManifestConsistency, P5CompleteGateIncludesSqlAudit) {
  std::ifstream in(EBTEST_TEST_MANIFEST);
  ASSERT_TRUE(in.good()) << EBTEST_TEST_MANIFEST;
  std::stringstream buffer;
  buffer << in.rdbuf();
  const auto gate_suites = ParseGateSuites(buffer.str(), "P5-complete");
  ASSERT_FALSE(gate_suites.empty());
  bool seen_sql = false;
  bool seen_audit = false;
  for (const auto& suite : gate_suites) {
    if (suite == "sql") seen_sql = true;
    if (suite == "audit") seen_audit = true;
  }
  EXPECT_TRUE(seen_sql);
  EXPECT_TRUE(seen_audit);
}

TEST(EbManifestConsistency, P4GateReferencesKnownSuites) {
  std::ifstream in(EBTEST_TEST_MANIFEST);
  ASSERT_TRUE(in.good()) << EBTEST_TEST_MANIFEST;
  std::stringstream buffer;
  buffer << in.rdbuf();
  const auto gate_suites = ParseGateSuites(buffer.str(), "P4");
  ASSERT_FALSE(gate_suites.empty());
  std::set<std::string> registered;
  for (int i = 0; i < kEbTestSuiteCount; ++i) {
    registered.insert(kEbTestSuites[i].name);
  }
  for (const auto& suite : gate_suites) {
    const auto slash = suite.find('/');
    const std::string base =
        slash == std::string::npos ? suite : suite.substr(0, slash);
    EXPECT_TRUE(registered.count(base) > 0) << suite;
  }
}

TEST(EbManifestConsistency, P4CompleteGateReferencesKnownSuites) {
  std::ifstream in(EBTEST_TEST_MANIFEST);
  ASSERT_TRUE(in.good()) << EBTEST_TEST_MANIFEST;
  std::stringstream buffer;
  buffer << in.rdbuf();
  const auto gate_suites = ParseGateSuites(buffer.str(), "P4-complete");
  ASSERT_FALSE(gate_suites.empty());
  std::set<std::string> registered;
  for (int i = 0; i < kEbTestSuiteCount; ++i) {
    registered.insert(kEbTestSuites[i].name);
  }
  for (const auto& suite : gate_suites) {
    const auto slash = suite.find('/');
    const std::string base =
        slash == std::string::npos ? suite : suite.substr(0, slash);
    EXPECT_TRUE(registered.count(base) > 0) << suite;
  }
}

TEST(EbManifestConsistency, P3CompleteGateReferencesKnownSuites) {
  std::ifstream in(EBTEST_TEST_MANIFEST);
  ASSERT_TRUE(in.good()) << EBTEST_TEST_MANIFEST;
  std::stringstream buffer;
  buffer << in.rdbuf();
  const auto gate_suites = ParseGateSuites(buffer.str(), "P3-complete");
  ASSERT_FALSE(gate_suites.empty());
  std::set<std::string> registered;
  for (int i = 0; i < kEbTestSuiteCount; ++i) {
    registered.insert(kEbTestSuites[i].name);
  }
  for (const auto& suite : gate_suites) {
    const auto slash = suite.find('/');
    const std::string base =
        slash == std::string::npos ? suite : suite.substr(0, slash);
    EXPECT_TRUE(registered.count(base) > 0) << suite;
  }
}

namespace {

bool GtestGlobMatch(const std::string& pattern, const std::string& name,
                    size_t pi = 0, size_t ni = 0) {
  if (pi == pattern.size()) return ni == name.size();
  if (pattern[pi] == '*') {
    for (size_t skip = ni; skip <= name.size(); ++skip) {
      if (GtestGlobMatch(pattern, name, pi + 1, skip)) return true;
    }
    return false;
  }
  if (ni >= name.size()) return false;
  if (pattern[pi] == '?' || pattern[pi] == name[ni]) {
    return GtestGlobMatch(pattern, name, pi + 1, ni + 1);
  }
  return false;
}

bool GtestFilterMatches(const std::string& filter, const std::string& test_name) {
  std::string positive = filter;
  std::vector<std::string> negatives;
  const auto dash = filter.find('-');
  if (dash != std::string::npos) {
    positive = filter.substr(0, dash);
    std::stringstream neg_ss(filter.substr(dash + 1));
    std::string neg;
    while (std::getline(neg_ss, neg, ':')) {
      if (!neg.empty()) negatives.push_back(neg);
    }
  }

  bool matched_positive = positive.empty();
  if (!positive.empty()) {
    std::stringstream pos_ss(positive);
    std::string pos;
    while (std::getline(pos_ss, pos, ':')) {
      if (pos.empty()) continue;
      if (GtestGlobMatch(pos, test_name)) {
        matched_positive = true;
        break;
      }
    }
  }
  if (!matched_positive) return false;

  for (const auto& neg : negatives) {
    if (GtestGlobMatch(neg, test_name)) return false;
  }
  return true;
}

std::string ReadRepoFile(const char* rel_path) {
  std::ifstream in(rel_path);
  if (!in.good()) return {};
  std::stringstream buffer;
  buffer << in.rdbuf();
  return buffer.str();
}

}  // namespace

TEST(EbManifestConsistency, GateGtestFilterMatchesTests) {
  const std::string no_fallback_case = "EbMatrix/NoFallbackMatrixTest.RunCase/0";
  const std::string schema_case = "EbMatrixSchema.NoFallbackCasesNonEmpty";
  const std::string write_guard = "KvRarMonitor.WriteCircuitBlocksPut";
  const std::string stability = "RarStability.WriteGuardSurvivesPowerfailReopen";
  const std::string heavy_oracle = "RarOracleEquivalence.ProductionRandomDestroy";

  EXPECT_TRUE(GtestFilterMatches("EbMatrix/NoFallbackMatrixTest.*", no_fallback_case));
  EXPECT_TRUE(GtestFilterMatches("EbMatrixSchema.NoFallbackCasesNonEmpty", schema_case));
  EXPECT_FALSE(GtestFilterMatches("EbMatrix.NoFallbackMatrixTest.*", no_fallback_case));
  EXPECT_FALSE(GtestFilterMatches("NoFallbackMatrixTest.*", no_fallback_case));

  const std::string run_tests = ReadRepoFile("scripts/test/run_tests.ps1");
  ASSERT_FALSE(run_tests.empty());
  EXPECT_NE(run_tests.find("EbMatrix/NoFallbackMatrixTest.*:EbMatrixSchema.NoFallbackCasesNonEmpty"),
            std::string::npos);
  EXPECT_NE(run_tests.find("--suite=matrix/no_fallback"),
            std::string::npos);
  EXPECT_EQ(run_tests.find("EbMatrix.NoFallbackMatrixTest.*"), std::string::npos);

  EXPECT_TRUE(GtestFilterMatches("KvRarMonitor*", write_guard));
  EXPECT_TRUE(GtestFilterMatches("RarStability*", stability));
  EXPECT_TRUE(GtestFilterMatches(
      "KvRarMonitor*:RarStability*:-RarOracleEquivalence.ProductionRandomDestroy",
      stability));
  EXPECT_FALSE(GtestFilterMatches(
      "KvRarMonitor*:RarStability*:-RarOracleEquivalence.ProductionRandomDestroy",
      heavy_oracle));

  const std::string manifest = ReadRepoFile(EBTEST_TEST_MANIFEST);
  ASSERT_FALSE(manifest.empty());
  EXPECT_NE(manifest.find("NoFallbackMatrixTest.RunCase"), std::string::npos);
  EXPECT_EQ(manifest.find("EbMatrix.NoFallbackMatrixTest.RunCase"), std::string::npos);

  const auto p18_suites = ParseGateSuites(manifest, "P18-stability");
  ASSERT_FALSE(p18_suites.empty());
  EXPECT_TRUE(std::find(p18_suites.begin(), p18_suites.end(), "audit") !=
              p18_suites.end());

  const std::string vcs_case = "VcsChainTest.AppendFloorAndTombstonePath";
  EXPECT_TRUE(GtestFilterMatches("VcsChain*:SnapshotResolver*:VcsPowerfail*:"
                                 "VcsSnapshotPowerfail*:LsvPerfRegression*:"
                                 "SqlTxn.SnapshotReadOwnWriteBeforeCommit*:"
                                 "SnapshotSiSql.*",
                                 vcs_case));
  const auto p19_suites = ParseGateSuites(manifest, "P19-lsv");
  ASSERT_FALSE(p19_suites.empty());
  EXPECT_NE(run_tests.find("P19-lsv"), std::string::npos);
  EXPECT_NE(run_tests.find("VcsChain*:SnapshotResolver*:VcsPowerfail*"),
            std::string::npos);

  const auto p20_suites = ParseGateSuites(manifest, "P20-lsv-tso");
  ASSERT_FALSE(p20_suites.empty());
  EXPECT_NE(run_tests.find("P20-lsv-tso"), std::string::npos);
  EXPECT_TRUE(GtestFilterMatches("SnapshotOccSql.*:SnapshotPhantomSql.*:"
                                 "Tsl3Lock*:SfsRead*:TxnWal*",
                                 "SnapshotOccSql.ConcurrentUpdateSecondCommitConflicts"));
}
