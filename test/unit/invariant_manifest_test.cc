#include <fstream>
#include <gtest/gtest.h>

#include <regex>
#include <sstream>
#include <string>
#include <vector>

#ifndef EBTEST_INVARIANT_MANIFEST
#define EBTEST_INVARIANT_MANIFEST "Docs/invariants/INVARIANT_MANIFEST.yaml"
#endif

namespace {

std::vector<std::string> ParseInvariantIds(const std::string& yaml) {
  std::vector<std::string> ids;
  std::istringstream in(yaml);
  std::string line;
  while (std::getline(in, line)) {
    const auto pos = line.find("id:");
    if (pos == std::string::npos) continue;
    std::string id = line.substr(pos + 3);
    while (!id.empty() && (id.front() == ' ' || id.front() == '\t')) {
      id.erase(id.begin());
    }
    if (!id.empty()) ids.push_back(id);
  }
  return ids;
}

std::string ReadTestField(const std::string& yaml, const std::string& id) {
  const std::string needle = "- id: " + id;
  const auto start = yaml.find(needle);
  if (start == std::string::npos) return "";
  const auto test_pos = yaml.find("test:", start);
  if (test_pos == std::string::npos) return "";
  const auto line_end = yaml.find('\n', test_pos);
  std::string line = yaml.substr(test_pos + 5, line_end - test_pos - 5);
  while (!line.empty() && (line.front() == ' ' || line.front() == '\t')) {
    line.erase(line.begin());
  }
  return line;
}

}  // namespace

TEST(EbInvariantManifest, AllInvariantsHaveTests) {
  std::ifstream in(EBTEST_INVARIANT_MANIFEST);
  ASSERT_TRUE(in.good()) << EBTEST_INVARIANT_MANIFEST;
  std::stringstream buffer;
  buffer << in.rdbuf();
  const std::string content = buffer.str();
  const auto ids = ParseInvariantIds(content);
  ASSERT_GE(ids.size(), 14u);
  for (const auto& id : ids) {
    const std::string test_ref = ReadTestField(content, id);
    EXPECT_FALSE(test_ref.empty()) << id;
    EXPECT_NE(test_ref.find('.'), std::string::npos) << id;
  }
}
