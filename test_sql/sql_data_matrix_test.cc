#include <gtest/gtest.h>

#include <sstream>
#include <string>
#include <vector>

#include "engine_test_util.h"
#include "sql/ast/advanced_ast.h"
#include "sql/ast/query_ast.h"
#include "sql/parse/native/native_parser.h"
#include "sql/session/database.h"
#include "sql/exec/mic_guard.h"
#include "generated/sql_matrix_inc.h"

namespace ebtree {
namespace sql {
namespace {

QueryStmtKind ParseKindName(const char* name) {
  const std::string n(name);
  if (n == "kCreateTable") return QueryStmtKind::kCreateTable;
  if (n == "kCreateIndex") return QueryStmtKind::kCreateIndex;
  if (n == "kDropIndex") return QueryStmtKind::kDropIndex;
  if (n == "kUpsert") return QueryStmtKind::kUpsert;
  if (n == "kSelect") return QueryStmtKind::kSelect;
  if (n == "kInsert") return QueryStmtKind::kInsert;
  if (n == "kUpdate") return QueryStmtKind::kUpdate;
  if (n == "kDelete") return QueryStmtKind::kDelete;
  if (n == "kBeginTxn") return QueryStmtKind::kBeginTxn;
  if (n == "kCommit") return QueryStmtKind::kCommit;
  if (n == "kRollback") return QueryStmtKind::kRollback;
  if (n == "kSavepoint") return QueryStmtKind::kSavepoint;
  if (n == "kWithCte") return QueryStmtKind::kWithCte;
  if (n == "kSetOp") return QueryStmtKind::kSetOp;
  if (n == "kWindowSelect") return QueryStmtKind::kWindowSelect;
  if (n == "kShow") return QueryStmtKind::kShow;
  if (n == "kSet") return QueryStmtKind::kSet;
  if (n == "kGrant") return QueryStmtKind::kGrant;
  if (n == "kExplain") return QueryStmtKind::kExplain;
  return QueryStmtKind::kUnknown;
}

bool HasSubquery(const ExprNode* node) {
  if (!node) return false;
  if (node->kind == ExprKind::kSubquery) return true;
  for (const auto& ch : node->children) {
    if (HasSubquery(ch.get())) return true;
  }
  return false;
}

std::vector<std::string> SplitSetups(const char* setups) {
  std::vector<std::string> out;
  std::stringstream ss(setups);
  std::string item;
  while (std::getline(ss, item, ';')) {
    if (!item.empty()) out.push_back(item);
  }
  return out;
}

void AssertAdvancedAstFields(const SqlParseMatrixCase& c, const QueryStatement& stmt) {
  if (stmt.kind == QueryStmtKind::kWithCte) {
    EXPECT_TRUE(stmt.cte_query.has_value()) << c.id;
    if (stmt.cte_query.has_value()) {
      EXPECT_FALSE(stmt.cte_query->ctes.empty()) << c.id;
      EXPECT_TRUE(stmt.cte_query->main_query) << c.id;
    }
  } else if (stmt.kind == QueryStmtKind::kSetOp) {
    EXPECT_TRUE(stmt.setop_query.has_value()) << c.id;
    if (stmt.setop_query.has_value()) {
      EXPECT_TRUE(stmt.setop_query->left) << c.id;
      EXPECT_TRUE(stmt.setop_query->right) << c.id;
    }
  } else if (stmt.kind == QueryStmtKind::kWindowSelect) {
    EXPECT_TRUE(stmt.window_query.has_value()) << c.id;
    if (stmt.window_query.has_value()) {
      EXPECT_FALSE(stmt.window_query->window_func.empty()) << c.id;
      EXPECT_TRUE(stmt.window_query->query) << c.id;
    }
  }
}

TEST(SqlDataMatrix, MinSqlExecMatrixCases) {
  EXPECT_GE(kSqlExecMatrixCaseCount, 150);
}

TEST(SqlDataMatrix, MinSqlMatrixCases) {
  const int total = kSqlParseMatrixCaseCount + kSqlExecMatrixCaseCount +
                    kSqlPerfMatrixCaseCount;
  EXPECT_GE(total, 200);
}

TEST(SqlDataMatrix, ParseCases) {
  parse::NativeParser parser;
  for (int i = 0; i < kSqlParseMatrixCaseCount; ++i) {
    const auto& c = kSqlParseMatrixCases[i];
    QueryStatement stmt{};
    ASSERT_TRUE(parser.Parse(c.sql, &stmt).ok()) << c.id;
    EXPECT_EQ(stmt.kind, ParseKindName(c.expect_kind)) << c.id;
    if (c.expect_joins >= 0) {
      ASSERT_TRUE(stmt.select_rich.has_value()) << c.id;
      EXPECT_EQ(stmt.select_rich->joins.size(),
                static_cast<size_t>(c.expect_joins)) << c.id;
    }
    if (c.expect_subquery >= 0 && stmt.select_rich) {
      EXPECT_EQ(HasSubquery(stmt.select_rich->where.get()), c.expect_subquery > 0)
          << c.id;
    }
    AssertAdvancedAstFields(c, stmt);
  }
}

TEST(SqlDataMatrix, ExecCases) {
  for (int i = 0; i < kSqlExecMatrixCaseCount; ++i) {
    const auto& c = kSqlExecMatrixCases[i];
    const std::string dir = test::TempDir("sql_matrix_exec_" + std::string(c.id));
    OpenOptions opts{};
    opts.path = dir;
    std::unique_ptr<Database> db;
    ASSERT_TRUE(Database::Open(opts, &db).ok()) << c.id;

    for (const auto& setup : SplitSetups(c.setups)) {
      ASSERT_TRUE(db->ExecuteSql(setup).ok()) << c.id << " setup: " << setup;
    }

    ExecuteResult result{};
    const Status st =
        c.follow_sql[0] != '\0' ? db->ExecuteSql(c.exec_sql)
                                : db->ExecuteSql(c.exec_sql, &result);
    if (c.expect == "mic_violation") {
      EXPECT_FALSE(st.ok()) << c.id;
      EXPECT_TRUE(IsMicContractViolation(st)) << c.id;
    } else if (c.expect == "ok") {
      EXPECT_TRUE(st.ok()) << c.id;
    } else {
      EXPECT_FALSE(st.ok()) << c.id;
    }

    if (c.follow_sql[0] != '\0') {
      ExecuteResult follow{};
      ASSERT_TRUE(db->ExecuteSql(c.follow_sql, &follow).ok()) << c.id;
      if (c.expect_rows >= 0) {
        EXPECT_EQ(follow.rows.size(), static_cast<size_t>(c.expect_rows)) << c.id;
      }
      if (c.expect_value[0] != '\0' && !follow.rows.empty()) {
        EXPECT_EQ(follow.rows[0].value, c.expect_value) << c.id;
      }
    } else if (c.expect == "ok" && c.expect_rows >= 0) {
      EXPECT_EQ(result.rows.size(), static_cast<size_t>(c.expect_rows)) << c.id;
    }
  }
}

}  // namespace
}  // namespace sql
}  // namespace ebtree
