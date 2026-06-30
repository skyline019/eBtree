#include <gtest/gtest.h>

#include "sql/ast/query_ast.h"
#include "sql/ast/advanced_ast.h"
#include "sql/parse/native/native_parser.h"
#include "sql/parse/registry/registry_parser.h"

namespace ebtree {
namespace sql {
namespace parse {
namespace {

TEST(SqlParseAdvanced, NestedExistsParseOnly) {
  parse::NativeParser parser;
  const char* sql =
      "SELECT a.key FROM a WHERE EXISTS (SELECT b.key FROM b WHERE b.key = a.key "
      "AND EXISTS (SELECT a.key FROM a WHERE a.key = b.key))";
  QueryStatement stmt{};
  ASSERT_TRUE(parser.Parse(sql, &stmt).ok());
  ASSERT_TRUE(stmt.select_rich.has_value());
  ASSERT_TRUE(stmt.select_rich->where);
}

TEST(SqlParseAdvanced, AdvancedAstFields) {
  RegistryParser parser;
  {
    QueryStatement stmt{};
    ASSERT_TRUE(parser.Parse(
                      "WITH cte AS (SELECT key FROM t) SELECT key FROM cte",
                      &stmt)
                      .ok());
    EXPECT_EQ(stmt.kind, QueryStmtKind::kWithCte);
    ASSERT_TRUE(stmt.cte_query.has_value());
    EXPECT_EQ(stmt.cte_query->ctes.size(), 1u);
    EXPECT_EQ(stmt.cte_query->ctes[0].name, "cte");
    ASSERT_TRUE(stmt.cte_query->main_query);
    EXPECT_EQ(stmt.cte_query->main_query->from_table, "cte");
  }
  {
    QueryStatement stmt{};
    ASSERT_TRUE(parser.Parse("SELECT a FROM t1 UNION SELECT b FROM t2", &stmt).ok());
    EXPECT_EQ(stmt.kind, QueryStmtKind::kSetOp);
    ASSERT_TRUE(stmt.setop_query.has_value());
    EXPECT_EQ(stmt.setop_query->op, SetOpKind::kUnion);
    ASSERT_TRUE(stmt.setop_query->left);
    ASSERT_TRUE(stmt.setop_query->right);
  }
  {
    QueryStatement stmt{};
    ASSERT_TRUE(parser.Parse(
                      "SELECT ROW_NUMBER() OVER (ORDER BY a) FROM t", &stmt)
                      .ok());
    EXPECT_EQ(stmt.kind, QueryStmtKind::kWindowSelect);
    ASSERT_TRUE(stmt.window_query.has_value());
    EXPECT_EQ(stmt.window_query->window_func, "ROW_NUMBER");
    ASSERT_TRUE(stmt.window_query->query);
    EXPECT_EQ(stmt.window_query->query->from_table, "t");
  }
}

TEST(SqlParseAdvanced, TxnAndAdvancedExecutable) {
  RegistryParser parser;
  const char* sqls[] = {
      "BEGIN",
      "BEGIN TRANSACTION",
      "COMMIT",
      "ROLLBACK",
      "SAVEPOINT sp1",
      "WITH cte AS (SELECT 1) SELECT * FROM cte",
      "SELECT a FROM t1 UNION SELECT b FROM t2",
      "SELECT ROW_NUMBER() OVER (ORDER BY a) FROM t",
  };
  for (const char* sql : sqls) {
    QueryStatement stmt{};
    ASSERT_TRUE(parser.Parse(sql, &stmt).ok()) << sql;
    EXPECT_FALSE(IsParseOnlyKind(stmt.kind)) << sql;
  }
}

TEST(SqlParseAdvanced, AdminParseOnly) {
  RegistryParser parser;
  const char* sqls[] = {
      "SHOW TABLES",
      "SET autocommit = 1",
      "GRANT SELECT ON t TO user1",
  };
  for (const char* sql : sqls) {
    QueryStatement stmt{};
    ASSERT_TRUE(parser.Parse(sql, &stmt).ok()) << sql;
    EXPECT_TRUE(IsParseOnlyKind(stmt.kind)) << sql;
  }
}

TEST(SqlParseAdvanced, BulkRegistryOltp) {
  RegistryParser parser;
  int ok_count = 0;
  for (int i = 0; i < 200; ++i) {
    QueryStatement stmt{};
    const std::string sql = "INSERT INTO t (key, value) VALUES ('k" +
                            std::to_string(i) + "', 'v" +
                            std::to_string(i) + "')";
    if (parser.Parse(sql, &stmt).ok()) ++ok_count;
  }
  for (int i = 0; i < 200; ++i) {
    QueryStatement stmt{};
    const std::string sql =
        "SELECT key, value FROM t WHERE key = 'k" + std::to_string(i) + "'";
    if (parser.Parse(sql, &stmt).ok()) ++ok_count;
  }
  for (int i = 0; i < 100; ++i) {
    QueryStatement stmt{};
    const std::string sql = "UPDATE t SET value = 'x" + std::to_string(i) +
                            "' WHERE key = 'k" + std::to_string(i) + "'";
    if (parser.Parse(sql, &stmt).ok()) ++ok_count;
  }
  for (int i = 0; i < 50; ++i) {
    QueryStatement stmt{};
    const std::string sql =
        "SELECT a.key FROM a JOIN b ON a.key = b.key WHERE a.key = 'k" +
        std::to_string(i) + "'";
    if (parser.Parse(sql, &stmt).ok()) ++ok_count;
  }
  EXPECT_GE(ok_count, 500);
}

TEST(SqlParseAdvanced, IndexBetweenLabel110Parse) {
  NativeParser parser;
  const char* sql =
      "SELECT pk FROM tab0 WHERE ((((col4 IS NULL OR (col3 < 642) AND col0 IN "
      "(SELECT col3 FROM tab0 WHERE (col3 > 259 OR col3 >= 572 OR ((((col3 < "
      "245 OR (col3 BETWEEN 960 AND 132 OR col1 <= 687.32) AND col0 <= 447 AND "
      "col0 > 605 AND col4 < 748.82)) AND (col3 > 437))) AND ((((col3 < 775 "
      "OR col0 < 208 AND ((col0 = 499 OR col4 = 376.71)))))) AND (col4 > "
      "919.51) AND col3 < 498 OR (col3 < 429) AND col3 BETWEEN 621 AND 669 OR "
      "col3 <= 554 OR (((col1 IN (655.32,811.71,707.89,506.69,604.75,863.62))) "
      "AND col3 > 608) OR ((((col0 < 210)) AND col0 < 918)) AND (col0 >= 159) "
      "AND col0 <= 404))))))";
  QueryStatement stmt{};
  const Status st = parser.Parse(sql, &stmt);
  ASSERT_TRUE(st.ok()) << st.message();
  ASSERT_TRUE(stmt.select_rich.has_value());
  ASSERT_TRUE(stmt.select_rich->where);
}

void CollectSubqueries(const ExprNode& node,
                       std::vector<const SubquerySpec*>* out) {
  if (node.kind == ExprKind::kSubquery && node.subquery.has_value()) {
    out->push_back(&*node.subquery);
  }
  for (const auto& ch : node.children) {
    CollectSubqueries(*ch, out);
  }
}

TEST(SqlParseAdvanced, IndexBetweenLabel110ParseDeep) {
  NativeParser parser;
  const char* sql =
      "SELECT pk FROM tab0 WHERE ((((col4 IS NULL OR (col3 < 642) AND col0 IN "
      "(SELECT col3 FROM tab0 WHERE (col3 > 259 OR col3 >= 572 OR ((((col3 < "
      "245 OR (col3 BETWEEN 960 AND 132 OR col1 <= 687.32) AND col0 <= 447 AND "
      "col0 > 605 AND col4 < 748.82)) AND (col3 > 437))) AND ((((col3 < 775 "
      "OR col0 < 208 AND ((col0 = 499 OR col4 = 376.71)))))) AND (col4 > "
      "919.51) AND col3 < 498 OR (col3 < 429) AND col3 BETWEEN 621 AND 669 OR "
      "col3 <= 554 OR (((col1 IN (655.32,811.71,707.89,506.69,604.75,863.62))) "
      "AND col3 > 608) OR ((((col0 < 210)) AND col0 < 918)) AND (col0 >= 159) "
      "AND col0 <= 404))))))";
  QueryStatement stmt{};
  ASSERT_TRUE(parser.Parse(sql, &stmt).ok());
  std::vector<const SubquerySpec*> subs;
  CollectSubqueries(*stmt.select_rich->where, &subs);
  ASSERT_GE(subs.size(), 1u);
  for (const SubquerySpec* sq : subs) {
    EXPECT_TRUE(sq->parsed_query != nullptr)
        << "missing parsed_query for subquery sql prefix: "
        << sq->sql.substr(0, 40);
  }
}

TEST(SqlParseAdvanced, IndexBetweenLabel110InnerSelectParse) {
  NativeParser parser;
  const char* inner =
      "SELECT col3 FROM tab0 WHERE (col3 > 259 OR col3 >= 572 OR ((((col3 < "
      "245 OR (col3 BETWEEN 960 AND 132 OR col1 <= 687.32) AND col0 <= 447 AND "
      "col0 > 605 AND col4 < 748.82)) AND (col3 > 437))) AND ((((col3 < 775 "
      "OR col0 < 208 AND ((col0 = 499 OR col4 = 376.71)))))) AND (col4 > "
      "919.51) AND col3 < 498 OR (col3 < 429) AND col3 BETWEEN 621 AND 669 OR "
      "col3 <= 554 OR (((col1 IN (655.32,811.71,707.89,506.69,604.75,863.62))) "
      "AND col3 > 608) OR ((((col0 < 210)) AND col0 < 918)) AND (col0 >= 159) "
      "AND col0 <= 404))";
  QueryStatement stmt{};
  const Status st = parser.Parse(inner, &stmt);
  ASSERT_TRUE(st.ok()) << st.message();
  ASSERT_TRUE(stmt.select_rich.has_value());
  ASSERT_TRUE(stmt.select_rich->where);
}

}  // namespace
}  // namespace parse
}  // namespace sql
}  // namespace ebtree
