#include <fstream>
#include <gtest/gtest.h>

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
