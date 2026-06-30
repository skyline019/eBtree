#pragma once

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <fstream>
#include <string>
#include <vector>

#include "engine_test_util.h"
#include "sql/eval/sql_value.h"
#include "sql/session/database.h"

namespace ebtree {
namespace test {
namespace sqllogic {

struct SqllogicCase {
  std::string name;
  std::string source;
  std::string coltypes;
  std::vector<std::string> setup;
  std::string sql;
  std::vector<std::string> expected_rows;
  bool expect_error{false};
  bool sort_rows{false};
};

inline std::vector<SqllogicCase> LoadSqllogicFile(const std::string& path) {
  std::ifstream in(path);
  std::vector<SqllogicCase> cases;
  if (!in) return cases;
  SqllogicCase cur{};
  std::string line;
  bool in_expected = false;
  while (std::getline(in, line)) {
    if (line.rfind("-- name:", 0) == 0) {
      if (!cur.name.empty() && !cur.sql.empty()) cases.push_back(cur);
      cur = SqllogicCase{};
      cur.name = line.substr(8);
      while (!cur.name.empty() && cur.name.front() == ' ') cur.name.erase(0, 1);
      in_expected = false;
      continue;
    }
    if (line.rfind("-- source:", 0) == 0) {
      cur.source = line.substr(10);
      while (!cur.source.empty() && cur.source.front() == ' ') {
        cur.source.erase(0, 1);
      }
      continue;
    }
    if (line.rfind("-- sort:", 0) == 0) {
      const std::string sort = line.substr(8);
      cur.sort_rows = sort.find("rowsort") != std::string::npos;
      continue;
    }
    if (line.rfind("-- coltypes:", 0) == 0) {
      cur.coltypes = line.substr(12);
      while (!cur.coltypes.empty() && cur.coltypes.front() == ' ') {
        cur.coltypes.erase(0, 1);
      }
      continue;
    }
    if (line == "----") {
      in_expected = true;
      continue;
    }
    if (line.rfind("-- setup:", 0) == 0) {
      cur.setup.push_back(line.substr(9));
      while (!cur.setup.back().empty() && cur.setup.back().front() == ' ') {
        cur.setup.back().erase(0, 1);
      }
      continue;
    }
    if (line.rfind("-- error", 0) == 0) {
      cur.expect_error = true;
      continue;
    }
    if (line.empty() || line[0] == '#') continue;
    if (line == "---") continue;
    if (in_expected) {
      cur.expected_rows.push_back(line);
    } else {
      cur.sql = line;
    }
  }
  if (!cur.name.empty() && !cur.sql.empty()) cases.push_back(cur);
  return cases;
}

inline std::string RowLine(const sql::SqlRow& row) {
  if (!row.key.empty() && !row.value.empty() && row.key != row.value) {
    return row.key + "|" + row.value;
  }
  if (!row.value.empty()) return row.value;
  return row.key;
}

inline bool RowMatches(const sql::SqlRow& row, const std::string& expected,
                       const std::string& coltypes = "") {
  if (expected == "NULL" && row.value.empty()) return true;
  const std::string line = RowLine(row);
  if (expected == line || expected == row.key || expected == row.value) {
    return true;
  }
  if (!coltypes.empty() && coltypes.size() == 1) {
    const char ct = coltypes[0];
    const sql::SqlValue actual =
        sql::SqlValue::FromLegacyString(row.value.empty() ? line : row.value);
    const std::string formatted = actual.ToDisplayString(ct);
    if (expected == formatted) return true;
  }
  if (row.key + "|" + row.value == expected) return true;
  const auto bar = expected.find('|');
  if (bar != std::string::npos) {
    const std::string k = expected.substr(0, bar);
    const std::string v = expected.substr(bar + 1);
    if (row.key == k && row.value == v) return true;
    if (bar == expected.rfind('|') && row.key == k && row.value == v) return true;
  }
  return false;
}

enum class FailKind {
  kPass,
  kOpenFail,
  kSetupFail,
  kExecFail,
  kRowCountMismatch,
  kRowMismatch,
};

struct CaseResult {
  FailKind kind{FailKind::kPass};
  std::string detail;
  std::string source;
};

inline CaseResult RunCase(const SqllogicCase& c) {
  CaseResult out{};
  out.source = c.source;
  const std::string dir = TempDir("sqllogic_" + c.name);
  sql::OpenOptions opts{};
  opts.path = dir;
  std::unique_ptr<sql::Database> db;
  if (!sql::Database::Open(opts, &db).ok()) {
    out.kind = FailKind::kOpenFail;
    out.detail = "Database::Open failed";
    return out;
  }
  for (const auto& setup : c.setup) {
    if (!db->ExecuteSql(setup).ok()) {
      out.kind = FailKind::kSetupFail;
      out.detail = db->last_error().empty() ? setup : db->last_error();
      return out;
    }
  }
  sql::ExecuteResult result{};
  const auto st = db->ExecuteSql(c.sql, &result);
  if (c.expect_error) {
    if (!st.ok()) {
      out.kind = FailKind::kPass;
      return out;
    }
    out.kind = FailKind::kExecFail;
    out.detail = "expected error";
    return out;
  }
  if (!st.ok()) {
    out.kind = FailKind::kExecFail;
    out.detail = db->last_error().empty() ? st.message() : db->last_error();
    return out;
  }
  if (c.expected_rows.empty()) {
    out.kind = FailKind::kPass;
    return out;
  }
  std::vector<std::string> expected = c.expected_rows;
  std::vector<std::string> actual;
  actual.reserve(result.rows.size());
  for (const auto& row : result.rows) actual.push_back(RowLine(row));
  if (c.sort_rows) {
    std::sort(expected.begin(), expected.end());
    std::sort(actual.begin(), actual.end());
  }
  if (actual.size() != expected.size()) {
    out.kind = FailKind::kRowCountMismatch;
    out.detail = "expected_rows=" + std::to_string(expected.size()) +
                 " actual_rows=" + std::to_string(actual.size());
    return out;
  }
  for (size_t i = 0; i < expected.size(); ++i) {
    if (c.sort_rows) {
      if (expected[i] != actual[i]) {
        out.kind = FailKind::kRowMismatch;
        out.detail = "sorted expected=" + expected[i] + " actual=" + actual[i];
        return out;
      }
    } else {
      sql::SqlRow pseudo{};
      const auto bar = expected[i].find('|');
      if (bar == std::string::npos) {
        pseudo.key = expected[i];
        pseudo.value = expected[i];
      } else {
        pseudo.key = expected[i].substr(0, bar);
        pseudo.value = expected[i].substr(bar + 1);
      }
      if (!RowMatches(result.rows[i], expected[i], c.coltypes)) {
        out.kind = FailKind::kRowMismatch;
        out.detail = "expected=" + expected[i] + " actual=" + actual[i];
        return out;
      }
    }
  }
  out.kind = FailKind::kPass;
  return out;
}

inline const char* FailKindName(FailKind k) {
  switch (k) {
    case FailKind::kPass: return "pass";
    case FailKind::kOpenFail: return "open_fail";
    case FailKind::kSetupFail: return "setup_fail";
    case FailKind::kExecFail: return "exec_fail";
    case FailKind::kRowCountMismatch: return "row_count_mismatch";
    case FailKind::kRowMismatch: return "row_mismatch";
  }
  return "unknown";
}

}  // namespace sqllogic
}  // namespace test
}  // namespace ebtree
