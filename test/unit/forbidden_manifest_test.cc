#include <fstream>
#include <gtest/gtest.h>

#include <map>
#include <sstream>
#include <string>
#include <vector>

#ifndef EBTEST_TEST_MANIFEST
#define EBTEST_TEST_MANIFEST "test/TEST_MANIFEST.yaml"
#endif

namespace {

std::vector<std::string> ParseForbiddenEntries(const std::string& yaml) {
  std::vector<std::string> entries;
  const auto pos = yaml.rfind("forbidden:");
  if (pos == std::string::npos) return entries;
  std::string tail = yaml.substr(pos);
  if (tail.rfind("forbidden_coverage:", 0) == 0) {
    const auto next = tail.find("\nforbidden:");
    if (next != std::string::npos) tail = tail.substr(next + 1);
  }
  std::istringstream in(tail);
  std::string line;
  std::getline(in, line);
  while (std::getline(in, line)) {
    if (line.empty() || (line[0] != ' ' && line[0] != '-')) break;
    if (line.find("  - ") != 0) continue;
    std::string entry = line.substr(4);
    while (!entry.empty() && (entry.front() == ' ' || entry.front() == '\t')) {
      entry.erase(entry.begin());
    }
    if (!entry.empty()) entries.push_back(entry);
  }
  return entries;
}

std::map<std::string, std::string> ParseForbiddenCoverage(const std::string& yaml) {
  std::map<std::string, std::string> out;
  const auto pos = yaml.find("forbidden_coverage:");
  if (pos == std::string::npos) return out;
  const auto end = yaml.find("\nmin_total_tests:", pos);
  const std::string block =
      end == std::string::npos ? yaml.substr(pos) : yaml.substr(pos, end - pos);
  std::istringstream in(block);
  std::string line;
  while (std::getline(in, line)) {
    if (!line.empty() && line.back() == '\r') line.pop_back();
    if (line.empty() || line[0] != ' ') continue;
    if (line.find("forbidden_coverage:") != std::string::npos) continue;
    const auto colon = line.rfind(':');
    if (colon == std::string::npos) continue;
    std::string key = line.substr(0, colon);
    while (!key.empty() && (key.front() == ' ' || key.front() == '\t')) {
      key.erase(key.begin());
    }
    std::string value = line.substr(colon + 1);
    while (!value.empty() && (value.front() == ' ' || value.front() == '\t')) {
      value.erase(value.begin());
    }
    if (!key.empty() && !value.empty()) out[key] = value;
  }
  return out;
}

int ParseMinTotalTests(const std::string& yaml) {
  const auto pos = yaml.find("min_total_tests:");
  if (pos == std::string::npos) return 0;
  const auto end = yaml.find('\n', pos);
  std::string line = yaml.substr(pos + 16, end - pos - 16);
  while (!line.empty() && (line.front() == ' ' || line.front() == '\t')) {
    line.erase(line.begin());
  }
  return line.empty() ? 0 : std::stoi(line);
}

}  // namespace

TEST(EbForbiddenManifest, AllForbiddenHaveRegisteredTests) {
  std::ifstream in(EBTEST_TEST_MANIFEST);
  ASSERT_TRUE(in.good()) << EBTEST_TEST_MANIFEST;
  std::stringstream buffer;
  buffer << in.rdbuf();
  const std::string content = buffer.str();
  const auto entries = ParseForbiddenEntries(content);
  ASSERT_GE(entries.size(), 11u);
  const auto coverage = ParseForbiddenCoverage(content);
  ASSERT_GE(coverage.size(), entries.size());
  for (const auto& entry : entries) {
    const auto it = coverage.find(entry);
    ASSERT_NE(it, coverage.end()) << "missing test ref for forbidden: " << entry;
    EXPECT_NE(it->second.find('.'), std::string::npos) << entry;
  }
}

TEST(EbForbiddenManifest, MinTotalTestsDocumented) {
  std::ifstream in(EBTEST_TEST_MANIFEST);
  ASSERT_TRUE(in.good());
  std::stringstream buffer;
  buffer << in.rdbuf();
  EXPECT_GE(ParseMinTotalTests(buffer.str()), 165);
}
