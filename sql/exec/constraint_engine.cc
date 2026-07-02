#include "constraint_engine.h"

#include "sql/eval/schema_context.h"
#include "sql/ast/select_ast.h"
#include "sql/parse/core/token_cursor.h"
#include "sql/parse/core/lex_pipeline.h"
#include "sql/parse/native/expr_parse.h"

namespace ebtree {
namespace sql {

namespace {

Status EvalCheckExpr(const std::string& check_sql, ExprEval* eval,
                     const RowMap& row) {
  if (check_sql.empty() || !eval) return Status::Ok();
  parse::ExprParse ep;
  parse::TokenCursor cur(parse::TokenizeSql(check_sql));
  std::unique_ptr<ExprNode> expr;
  const Status ps = ep.ParsePredicate(&cur, &expr);
  if (!ps.ok()) return Status::Ok();
  if (!eval->EvalBool(*expr, row)) {
    return Status::InvalidArgument("CHECK constraint failed");
  }
  return Status::Ok();
}

}  // namespace

Status ValidateRowConstraints(
    const TableSchema& table,
    const std::unordered_map<std::string, std::string>& fields) {
  ExprEval eval;
  SchemaContext ctx;
  ctx.table = &table;
  eval.SetSchemaContext(ctx);
  return ValidateRowConstraintsWithEval(table, fields, &eval);
}

Status ValidateRowConstraintsWithEval(
    const TableSchema& table,
    const std::unordered_map<std::string, std::string>& fields,
    ExprEval* eval) {
  RowMap row(fields.begin(), fields.end());
  for (const auto& col : table.columns) {
    const auto it = fields.find(col.name);
    const std::string val = it != fields.end() ? it->second : "";
    if (col.not_null && val.empty()) {
      return Status::InvalidArgument("NOT NULL constraint failed: " + col.name);
    }
    if (!col.check_expr.empty()) {
      const Status cs = EvalCheckExpr(col.check_expr, eval, row);
      if (!cs.ok()) return cs;
    }
  }
  return Status::Ok();
}

}  // namespace sql
}  // namespace ebtree
