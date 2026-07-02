#include <gtest/gtest.h>

#include <set>
#include <string>
#include <vector>

#include "sqllogic_common.h"
#include "engine_test_util.h"
#include "sql/session/database.h"

namespace ebtree {
namespace test {
namespace {

using sqllogic::LoadSqllogicFile;
using sqllogic::RunCase;
using sqllogic::SqllogicCase;

int RunCorpusCases(const std::vector<SqllogicCase>& cases, double min_rate) {
  int passed = 0;
  std::vector<std::string> failed_names;
  for (const auto& c : cases) {
    if (RunCase(c).kind == sqllogic::FailKind::kPass) {
      ++passed;
    } else {
      failed_names.push_back(c.name);
    }
  }
  const double rate = cases.empty() ? 0.0
                                    : static_cast<double>(passed) /
                                          static_cast<double>(cases.size());
  if (rate < min_rate) {
    ADD_FAILURE() << "failed cases (" << failed_names.size() << "): "
                  << (failed_names.empty() ? "none" : failed_names[0]);
    for (size_t i = 1; i < failed_names.size() && i < 20; ++i) {
      ADD_FAILURE() << "  " << failed_names[i];
    }
  }
  EXPECT_GE(rate, min_rate) << "passed=" << passed << " total=" << cases.size();
  return passed;
}

std::vector<SqllogicCase> LoadSqllogicCorpus() {
  std::vector<SqllogicCase> all;
  const std::vector<std::string> paths = {
      "test/data/sqllogic/basic.test",
      "test/data/sqllogic/curated/generated.test",
  };
  for (const auto& p : paths) {
    const auto cases = LoadSqllogicFile(p);
    all.insert(all.end(), cases.begin(), cases.end());
  }
  return all;
}

TEST(SqllogicRunner, CuratedSubsetPassRate) {
  const auto cases = LoadSqllogicFile("test/data/sqllogic/basic.test");
  ASSERT_FALSE(cases.empty());
  (void)RunCorpusCases(cases, 0.60);
}

TEST(SqllogicRunner, ProgramCompletePassRate) {
  const auto cases = LoadSqllogicFile("test/data/sqllogic/basic.test");
  ASSERT_FALSE(cases.empty());
  (void)RunCorpusCases(cases, 0.90);
}

TEST(SqllogicRunner, CuratedCorpusPassRate) {
  const auto cases = LoadSqllogicCorpus();
  ASSERT_GE(cases.size(), 500u);
  (void)RunCorpusCases(cases, 0.90);
}

TEST(SqllogicRunner, RealSqliteOfficialPassRate) {
  const auto cases = LoadSqllogicFile("test/data/sqllogic/sqlite/imported.test");
  if (cases.empty()) {
    GTEST_SKIP() << "missing imported.test (run import_sqlite_sqllogic.py first)";
  }
  ASSERT_GE(cases.size(), 800u);
  (void)RunCorpusCases(cases, 1.0);
}

const SqllogicCase* FindCaseByName(const std::vector<SqllogicCase>& cases,
                                   const std::string& name) {
  for (const auto& c : cases) {
    if (c.name == name) return &c;
  }
  return nullptr;
}

TEST(SqllogicRunner, IndexBetweenLabel110InnerExec) {
  const auto cases =
      LoadSqllogicFile("test/data/sqllogic/sqlite/imported.test");
  const SqllogicCase* base =
      FindCaseByName(cases, "index__between__100__slt_good_3_label-110");
  ASSERT_NE(base, nullptr);
  const std::string dir = TempDir("sqllogic_label110_inner");
  sql::OpenOptions opts{};
  opts.path = dir;
  std::unique_ptr<sql::Database> db;
  ASSERT_TRUE(sql::Database::Open(opts, &db).ok());
  for (const auto& setup : base->setup) {
    ASSERT_TRUE(db->ExecuteSql(setup).ok()) << setup;
  }
  const char* inner =
      "SELECT col3 FROM tab0 WHERE (col3 > 259 OR col3 >= 572 OR ((((col3 < "
      "245 OR (col3 BETWEEN 960 AND 132 OR col1 <= 687.32) AND col0 <= 447 "
      "AND col0 > 605 AND col4 < 748.82)) AND (col3 > 437))) AND ((((col3 < "
      "775 OR col0 < 208 AND ((col0 = 499 OR col4 = 376.71)))))) AND (col4 > "
      "919.51) AND col3 < 498 OR (col3 < 429) AND col3 BETWEEN 621 AND 669 OR "
      "col3 <= 554 OR (((col1 IN (655.32,811.71,707.89,506.69,604.75,863.62))) "
      "AND col3 > 608) OR ((((col0 < 210)) AND col0 < 918)) AND (col0 >= 159) "
      "AND col0 <= 404))";
  sql::ExecuteResult result{};
  ASSERT_TRUE(db->ExecuteSql(inner, &result).ok()) << db->last_error();
  EXPECT_GE(result.rows.size(), 5u)
      << "inner subquery returned " << result.rows.size() << " rows";
  std::set<std::string> inner_vals;
  for (const auto& row : result.rows) {
    inner_vals.insert(row.value.empty() ? row.key : row.value);
  }
  for (const char* need : {"525", "460", "4", "929", "293"}) {
    EXPECT_TRUE(inner_vals.count(need))
        << "inner missing col3=" << need << " needed for outer col0 IN match";
  }
}

TEST(SqllogicRunner, IndexBetweenLabel110SimpleIn) {
  const auto cases =
      LoadSqllogicFile("test/data/sqllogic/sqlite/imported.test");
  const SqllogicCase* base =
      FindCaseByName(cases, "index__between__100__slt_good_3_label-110");
  ASSERT_NE(base, nullptr);
  SqllogicCase c = *base;
  c.name = "index_between_label110_simple_in";
  c.sql = "SELECT pk FROM tab0 WHERE col0 IN (SELECT col3 FROM tab0)";
  c.sort_rows = true;
  c.expected_rows.clear();
  const std::string dir = TempDir("sqllogic_label110_simple_in");
  sql::OpenOptions opts{};
  opts.path = dir;
  std::unique_ptr<sql::Database> db;
  ASSERT_TRUE(sql::Database::Open(opts, &db).ok());
  for (const auto& setup : c.setup) {
    ASSERT_TRUE(db->ExecuteSql(setup).ok()) << setup;
  }
  sql::ExecuteResult result{};
  ASSERT_TRUE(db->ExecuteSql(c.sql, &result).ok()) << db->last_error();
  EXPECT_GT(result.rows.size(), 0u)
      << "simple col0 IN (SELECT col3) returned no rows";
}

TEST(SqllogicRunner, IndexBetweenLabel110OuterInOnly) {
  const auto cases =
      LoadSqllogicFile("test/data/sqllogic/sqlite/imported.test");
  const SqllogicCase* base =
      FindCaseByName(cases, "index__between__100__slt_good_3_label-110");
  ASSERT_NE(base, nullptr);
  SqllogicCase c = *base;
  c.name = "index_between_label110_outer_in";
  c.sql =
      "SELECT pk FROM tab0 WHERE (col3 < 642) AND col0 IN (SELECT col3 FROM "
      "tab0 WHERE (col3 > 259 OR col3 >= 572 OR ((((col3 < 245 OR (col3 "
      "BETWEEN 960 AND 132 OR col1 <= 687.32) AND col0 <= 447 AND col0 > 605 "
      "AND col4 < 748.82)) AND (col3 > 437))) AND ((((col3 < 775 OR col0 < "
      "208 AND ((col0 = 499 OR col4 = 376.71)))))) AND (col4 > 919.51) AND "
      "col3 < 498 OR (col3 < 429) AND col3 BETWEEN 621 AND 669 OR col3 <= "
      "554 OR (((col1 IN (655.32,811.71,707.89,506.69,604.75,863.62))) AND "
      "col3 > 608) OR ((((col0 < 210)) AND col0 < 918)) AND (col0 >= 159) "
      "AND col0 <= 404)))";
  c.expected_rows = {"35", "45", "73", "80", "93"};
  const auto r = RunCase(c);
  EXPECT_EQ(r.kind, sqllogic::FailKind::kPass) << r.detail;
}

TEST(SqllogicRunner, IndexBetweenLabel110Exec) {
  const auto cases =
      LoadSqllogicFile("test/data/sqllogic/sqlite/imported.test");
  ASSERT_FALSE(cases.empty());
  const SqllogicCase* c =
      FindCaseByName(cases, "index__between__100__slt_good_3_label-110");
  ASSERT_NE(c, nullptr);
  const auto r = RunCase(*c);
  EXPECT_EQ(r.kind, sqllogic::FailKind::kPass) << r.detail;
}

TEST(SqllogicRunner, IndexBetweenParenWhereWithNestedSelect) {
  SqllogicCase c{};
  c.name = "index_between_paren_select";
  c.coltypes = "I";
  c.sort_rows = true;
  c.setup = {
      "CREATE TABLE tab0(pk INTEGER PRIMARY KEY, col0 INTEGER, col1 FLOAT, col2 "
      "TEXT, col3 INTEGER, col4 FLOAT, col5 TEXT)",
      "INSERT INTO tab0 VALUES(0,22,43.96,'yoyca',0,80.14,'eoenc')",
      "INSERT INTO tab0 VALUES(3,67,90.66,'rnadc',77,50.36,'knooo')",
      "INSERT INTO tab0 VALUES(6,84,24.24,'ttodp',31,73.0,'wujjl')",
      "INSERT INTO tab0 VALUES(8,68,38.47,'kaoqh',8,41.5,'fyhzl')",
  };
  c.sql =
      "SELECT pk FROM tab0 WHERE (col4 > 25.60 OR col0 IS NULL AND (col1 IN "
      "(SELECT col4 FROM tab0)))";
  c.expected_rows = {"0", "3", "6", "8"};
  const auto r = RunCase(c);
  EXPECT_EQ(r.kind, sqllogic::FailKind::kPass) << r.detail;
}

}  // namespace
}  // namespace test
}  // namespace ebtree
