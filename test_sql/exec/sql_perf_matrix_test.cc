#include <gtest/gtest.h>

#include <sstream>
#include <string>
#include <vector>

#include "engine_test_util.h"
#include "sql/exec/mic_guard.h"
#include "sql/session/database.h"
#include "generated/sql_matrix_inc.h"

namespace ebtree {
namespace sql {
namespace {

std::vector<std::string> SplitSetups(const char* setups) {
  std::vector<std::string> out;
  std::stringstream ss(setups);
  std::string item;
  while (std::getline(ss, item, ';')) {
    if (!item.empty()) out.push_back(item);
  }
  return out;
}

TEST(SqlPerfMatrix, DataDrivenCases) {
  for (int i = 0; i < kSqlPerfMatrixCaseCount; ++i) {
    const auto& c = kSqlPerfMatrixCases[i];
    const std::string dir = test::TempDir("sql_perf_" + std::string(c.id));
    OpenOptions opts{};
    opts.path = dir;
    std::unique_ptr<Database> db;
    ASSERT_TRUE(Database::Open(opts, &db).ok()) << c.id;

    for (const auto& setup : SplitSetups(c.setups)) {
      ASSERT_TRUE(db->ExecuteSql(setup).ok()) << c.id << " setup: " << setup;
    }

    ExecuteResult result{};
    const Status st = db->ExecuteSql(c.exec_sql, &result);
    if (c.expect == "mic_violation") {
      EXPECT_FALSE(st.ok()) << c.id;
      EXPECT_TRUE(IsMicContractViolation(st)) << c.id;
    } else {
      EXPECT_TRUE(st.ok()) << c.id << " " << st.message();
      if (c.expect_rows >= 0) {
        EXPECT_EQ(result.rows.size(), static_cast<size_t>(c.expect_rows)) << c.id;
      }
    }
  }
}

}  // namespace
}  // namespace sql
}  // namespace ebtree
