#include <gtest/gtest.h>

#include "sql/parse/native/native_parser.h"
#include "sql/parse/registry/registry_parser.h"

namespace ebtree {
namespace sql {
namespace {

struct ParseCase {
  const char* sql;
  QueryStmtKind expected_kind;
};

const ParseCase kCases[] = {
    {"CREATE TABLE t1 (key TEXT PRIMARY KEY, value TEXT)",
     QueryStmtKind::kCreateTable},
    {"CREATE TABLE users (id TEXT, name TEXT)", QueryStmtKind::kCreateTable},
    {"INSERT INTO t (key, value) VALUES ('a', '1')", QueryStmtKind::kInsert},
    {"SELECT key, value FROM t WHERE key = 'a'", QueryStmtKind::kSelect},
    {"SELECT key, value FROM t /* @max_pages=16 */", QueryStmtKind::kSelect},
    {"DELETE FROM t WHERE key = 'a'", QueryStmtKind::kDelete},
    {"DROP TABLE t", QueryStmtKind::kDropTable},
    {"UPDATE t SET value = 'x' WHERE key = 'a'", QueryStmtKind::kUpdate},
    {"ALTER TABLE t ADD COLUMN c TEXT", QueryStmtKind::kAlterTable},
    {"OPEN DATABASE '/tmp/db' WITH DURABILITY BALANCED WITH ATTESTATION "
     "REQUIRE_PASS",
     QueryStmtKind::kOpen},
};

TEST(SqlParseMatrix, NativeParserGoldenSet) {
  parse::NativeParser parser;
  for (const auto& c : kCases) {
    QueryStatement stmt{};
    ASSERT_TRUE(parser.Parse(c.sql, &stmt).ok()) << c.sql;
    EXPECT_EQ(stmt.kind, c.expected_kind) << c.sql;
  }
}

TEST(SqlParseMatrix, RegistryParserGoldenSet) {
  parse::RegistryParser parser;
  for (const auto& c : kCases) {
    QueryStatement stmt{};
    ASSERT_TRUE(parser.Parse(c.sql, &stmt).ok()) << c.sql;
    EXPECT_EQ(stmt.kind, c.expected_kind) << c.sql;
  }
}

TEST(SqlParseMatrix, BulkInsertVariants) {
  parse::NativeParser parser;
  for (int i = 0; i < 50; ++i) {
    const std::string sql = "INSERT INTO bulk (key, value) VALUES ('k" +
                            std::to_string(i) + "', 'v" +
                            std::to_string(i) + "')";
    QueryStatement stmt{};
    ASSERT_TRUE(parser.Parse(sql, &stmt).ok());
    EXPECT_EQ(stmt.kind, QueryStmtKind::kInsert);
    EXPECT_EQ(stmt.insert.table, "bulk");
  }
}

TEST(SqlParseMatrix, BulkSelectVariants) {
  parse::NativeParser parser;
  for (int i = 0; i < 50; ++i) {
    const std::string sql =
        "SELECT key, value FROM t WHERE key = 'k" + std::to_string(i) + "'";
    QueryStatement stmt{};
    ASSERT_TRUE(parser.Parse(sql, &stmt).ok());
    EXPECT_EQ(stmt.kind, QueryStmtKind::kSelect);
    EXPECT_EQ(stmt.select.key, "k" + std::to_string(i));
  }
}

TEST(SqlParseMatrix, BulkUpdateVariants) {
  parse::NativeParser parser;
  for (int i = 0; i < 30; ++i) {
    const std::string sql = "UPDATE t SET value = 'v" + std::to_string(i) +
                            "' WHERE key = 'k" + std::to_string(i) + "'";
    QueryStatement stmt{};
    ASSERT_TRUE(parser.Parse(sql, &stmt).ok());
    EXPECT_EQ(stmt.kind, QueryStmtKind::kUpdate);
    EXPECT_EQ(stmt.update.set_value, "v" + std::to_string(i));
  }
}

TEST(SqlParseMatrix, BulkDeleteVariants) {
  parse::NativeParser parser;
  for (int i = 0; i < 30; ++i) {
    const std::string sql =
        "DELETE FROM t WHERE key = 'k" + std::to_string(i) + "'";
    QueryStatement stmt{};
    ASSERT_TRUE(parser.Parse(sql, &stmt).ok());
    EXPECT_EQ(stmt.kind, QueryStmtKind::kDelete);
  }
}

TEST(SqlParseMatrix, BulkDropTableVariants) {
  parse::NativeParser parser;
  for (int i = 0; i < 20; ++i) {
    const std::string sql = "DROP TABLE t" + std::to_string(i);
    QueryStatement stmt{};
    ASSERT_TRUE(parser.Parse(sql, &stmt).ok());
    EXPECT_EQ(stmt.kind, QueryStmtKind::kDropTable);
  }
}

TEST(SqlParseMatrix, JoinVariants) {
  parse::NativeParser parser;
  for (int i = 0; i < 10; ++i) {
    const std::string sql =
        "SELECT a.key FROM a JOIN b ON a.key = b.key WHERE a.key = 'k" +
        std::to_string(i) + "'";
    QueryStatement stmt{};
    ASSERT_TRUE(parser.Parse(sql, &stmt).ok()) << sql;
    EXPECT_EQ(stmt.kind, QueryStmtKind::kSelect);
    ASSERT_EQ(stmt.joins.size(), 1u);
    EXPECT_EQ(stmt.joins[0].table, "b");
  }
}

TEST(SqlParseMatrix, BulkAlterVariants) {
  parse::NativeParser parser;
  for (int i = 0; i < 20; ++i) {
    const std::string sql =
        "ALTER TABLE t" + std::to_string(i) + " ADD COLUMN c TEXT";
    QueryStatement stmt{};
    ASSERT_TRUE(parser.Parse(sql, &stmt).ok());
    EXPECT_EQ(stmt.kind, QueryStmtKind::kAlterTable);
  }
}

TEST(SqlParseMatrix, BulkOpenVariants) {
  parse::NativeParser parser;
  for (int i = 0; i < 10; ++i) {
    const std::string sql =
        "OPEN DATABASE '/tmp/db" + std::to_string(i) +
        "' WITH DURABILITY BALANCED";
    QueryStatement stmt{};
    ASSERT_TRUE(parser.Parse(sql, &stmt).ok());
    EXPECT_EQ(stmt.kind, QueryStmtKind::kOpen);
  }
}

TEST(SqlParseMatrix, BulkInExistsParse) {
  parse::NativeParser parser;
  for (int i = 0; i < 15; ++i) {
    const std::string sql =
        "SELECT key FROM t WHERE key IN (SELECT key FROM s WHERE key = 'k" +
        std::to_string(i) + "')";
    QueryStatement stmt{};
    ASSERT_TRUE(parser.Parse(sql, &stmt).ok()) << sql;
    EXPECT_EQ(stmt.kind, QueryStmtKind::kSelect);
    ASSERT_TRUE(stmt.select_rich.has_value());
    EXPECT_TRUE(stmt.select_rich->where != nullptr);
  }
}

TEST(SqlParseMatrix, BulkAdvancedParseVariants) {
  parse::NativeParser parser;
  for (int i = 0; i < 50; ++i) {
    {
      const std::string sql = "WITH c" + std::to_string(i) +
                              " AS (SELECT key FROM t) SELECT key FROM c" +
                              std::to_string(i);
      QueryStatement stmt{};
      ASSERT_TRUE(parser.Parse(sql, &stmt).ok()) << sql;
      EXPECT_EQ(stmt.kind, QueryStmtKind::kWithCte);
    }
    if (i % 2 == 0) {
      const std::string sql = "SELECT a FROM t1 UNION SELECT b FROM t" +
                              std::to_string(i);
      QueryStatement stmt{};
      ASSERT_TRUE(parser.Parse(sql, &stmt).ok()) << sql;
      EXPECT_EQ(stmt.kind, QueryStmtKind::kSetOp);
    }
  }
}

TEST(SqlParseMatrix, ThreeTableJoinParse) {
  parse::NativeParser parser;
  QueryStatement stmt{};
  const char* sql =
      "SELECT a.k FROM a JOIN b ON a.k = b.k JOIN c ON b.k = c.k";
  ASSERT_TRUE(parser.Parse(sql, &stmt).ok());
  EXPECT_TRUE(stmt.select_rich.has_value());
  EXPECT_EQ(stmt.select_rich->joins.size(), 2u);
}

}  // namespace
}  // namespace sql
}  // namespace ebtree
