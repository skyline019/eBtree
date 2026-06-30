#include <gtest/gtest.h>

#include <filesystem>
#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <map>
#include <string>

#include "sqllogic_common.h"

namespace ebtree {
namespace test {
namespace {

using sqllogic::CaseResult;
using sqllogic::FailKind;
using sqllogic::LoadSqllogicFile;
using sqllogic::RunCase;
using sqllogic::SqllogicCase;

std::string JsonEscape(const std::string& s) {
  std::string out;
  out.reserve(s.size() + 8);
  for (char c : s) {
    if (c == '"') out += "\\\"";
    else if (c == '\\') out += "\\\\";
    else if (c == '\n') out += "\\n";
    else out.push_back(c);
  }
  return out;
}

std::string BaselineReportPath() {
  const char* env = std::getenv("EBTREE_SQLLOGIC_BASELINE_OUT");
  if (env && env[0] != '\0') return env;
  return "build-msvc-2026/sqllogic_baseline.json";
}

void WriteBaselineReport(const std::vector<SqllogicCase>& cases,
                         const std::vector<CaseResult>& results,
                         const std::string& corpus) {
  int passed = 0;
  std::map<std::string, int> by_kind;
  std::map<std::string, int> by_source;
  for (const auto& r : results) {
    const char* name = sqllogic::FailKindName(r.kind);
    by_kind[name]++;
    if (r.kind == FailKind::kPass) {
      ++passed;
    } else if (!r.source.empty()) {
      const auto leaf = std::filesystem::path(r.source).filename().string();
      by_source[leaf]++;
    }
  }

  const std::string path = BaselineReportPath();
  const auto parent = std::filesystem::path(path).parent_path();
  if (!parent.empty()) {
    std::filesystem::create_directories(parent);
  }
  std::ofstream out(path, std::ios::trunc);
  out << "{\n";
  out << "  \"corpus\": \"" << JsonEscape(corpus) << "\",\n";
  out << "  \"total\": " << cases.size() << ",\n";
  out << "  \"passed\": " << passed << ",\n";
  out << "  \"pass_rate\": "
      << (cases.empty() ? 0.0
                        : static_cast<double>(passed) /
                              static_cast<double>(cases.size()))
      << ",\n";
  out << "  \"by_kind\": {";
  bool first = true;
  for (const auto& kv : by_kind) {
    if (!first) out << ',';
    first = false;
    out << "\n    \"" << kv.first << "\": " << kv.second;
  }
  out << "\n  },\n";
  out << "  \"failures_by_source_file\": {";
  first = true;
  for (const auto& kv : by_source) {
    if (!first) out << ',';
    first = false;
    out << "\n    \"" << JsonEscape(kv.first) << "\": " << kv.second;
  }
  out << "\n  },\n";
  out << "  \"sample_failures\": [\n";
  int samples = 0;
  for (size_t i = 0; i < results.size() && samples < 40; ++i) {
    if (results[i].kind == FailKind::kPass) continue;
    if (samples > 0) out << ",\n";
    out << "    {\"name\": \"" << JsonEscape(cases[i].name) << "\","
        << " \"kind\": \"" << sqllogic::FailKindName(results[i].kind) << "\","
        << " \"detail\": \"" << JsonEscape(results[i].detail) << "\","
        << " \"sql\": \"" << JsonEscape(cases[i].sql) << "\"}";
    ++samples;
  }
  out << "\n  ]\n}\n";
  out.close();

  std::cout << "sqllogic baseline: passed=" << passed << " total=" << cases.size()
            << " rate="
            << (cases.empty() ? 0.0
                              : 100.0 * static_cast<double>(passed) /
                                    static_cast<double>(cases.size()))
            << "%\n";
  std::cout << "  by_kind:";
  for (const auto& kv : by_kind) {
    std::cout << " " << kv.first << "=" << kv.second;
  }
  std::cout << "\n  report: " << path << "\n";
}

TEST(SqllogicRunner, RealSqliteBaselineReport) {
  const std::string path = "test/data/sqllogic/sqlite/imported.test";
  const auto cases = LoadSqllogicFile(path);
  if (cases.empty()) {
    GTEST_SKIP() << "missing " << path
                 << " (run scripts/test/import_sqlite_sqllogic.py first)";
  }

  std::vector<CaseResult> results;
  results.reserve(cases.size());
  for (const auto& c : cases) {
    results.push_back(RunCase(c));
  }
  WriteBaselineReport(cases, results, path);

  const int passed = static_cast<int>(
      std::count_if(results.begin(), results.end(),
                    [](const CaseResult& r) { return r.kind == FailKind::kPass; }));
  RecordProperty("passed", passed);
  RecordProperty("total", static_cast<int>(cases.size()));
  EXPECT_GT(cases.size(), 0u);
}

}  // namespace
}  // namespace test
}  // namespace ebtree
