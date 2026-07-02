#include "stmt_handlers.h"

#include <sstream>
#include <unordered_map>

#include "sql/parse/core/first_match_registry.h"
#include "sql/parse/core/lex_pipeline.h"
#include "sql/parse/core/parse_context.h"
#include "sql/parse/core/stmt_classifier.h"
#include "sql/parse/core/lex_pipeline.h"
#include "sql/parse/native/expr_parse.h"
#include "sql/catalog/row_codec.h"
#include "sql/parse/native/select_parse.h"
#include "sql/parse/shared/parse_shared.h"

namespace ebtree {
namespace sql {
namespace parse {

namespace {

QueryStmtKind MapParseOnlyKind(StmtClass cls) {
  switch (cls) {
    case StmtClass::kBeginTxn: return QueryStmtKind::kBeginTxn;
    case StmtClass::kCommit: return QueryStmtKind::kCommit;
    case StmtClass::kRollback: return QueryStmtKind::kRollback;
    case StmtClass::kSavepoint: return QueryStmtKind::kSavepoint;
    case StmtClass::kWithCte: return QueryStmtKind::kWithCte;
    case StmtClass::kSetOp: return QueryStmtKind::kSetOp;
    case StmtClass::kWindow: return QueryStmtKind::kWindowSelect;
    case StmtClass::kShow: return QueryStmtKind::kShow;
    case StmtClass::kSet: return QueryStmtKind::kSet;
    case StmtClass::kGrant: return QueryStmtKind::kGrant;
    case StmtClass::kExplain: return QueryStmtKind::kExplain;
    default: return QueryStmtKind::kUnknown;
  }
}

AttestationMode ParseAttestation(const std::string& token) {
  const std::string u = Upper(token);
  if (u == "REQUIRE_PASS") return AttestationMode::kRequirePass;
  if (u == "ALLOW_WARN") return AttestationMode::kAllowWarn;
  if (u == "MONITOR") return AttestationMode::kMonitor;
  return AttestationMode::kOff;
}

std::string ParseSqlValue(const std::string& tok) {
  if (Upper(tok) == "NULL") return "";
  return UnquoteToken(tok);
}

Status ParseParenValueList(TokenCursor* cur, std::vector<std::string>* values) {
  if (cur->Peek() != "(") {
    return Status::InvalidArgument("expected value list (");
  }
  cur->Consume(nullptr);
  while (!cur->AtEnd() && cur->Peek() != ")") {
    std::string tok;
    cur->Consume(&tok);
    values->push_back(ParseSqlValue(tok));
    if (cur->Peek() == ",") cur->Consume(nullptr);
  }
  if (cur->Peek() == ")") cur->Consume(nullptr);
  return Status::Ok();
}

Status ParseColumnNameList(TokenCursor* cur, std::vector<std::string>* cols) {
  if (cur->Peek() != "(") return Status::Ok();
  cur->Consume(nullptr);
  while (!cur->AtEnd() && cur->Peek() != ")") {
    std::string col;
    cur->Consume(&col);
    cols->push_back(col);
    if (cur->Peek() == ",") cur->Consume(nullptr);
  }
  if (cur->Peek() == ")") cur->Consume(nullptr);
  return Status::Ok();
}

void FillLegacyInsertKeyValue(InsertStmt* insert) {
  if (!insert || insert->values.empty()) return;
  insert->key = insert->values[0];
  if (insert->values.size() > 1) insert->value = insert->values[1];
}

}  // namespace

Status ParseParseOnly(ParseContext* ctx, StmtClass cls) {
  if (!ctx || !ctx->out) return Status::InvalidArgument("null context");
  ctx->out->kind = MapParseOnlyKind(cls);
  ctx->out->raw_sql = ctx->raw_sql;
  return Status::Ok();
}

Status ParseOpen(ParseContext* ctx) {
  ctx->out->kind = QueryStmtKind::kOpen;
  size_t pos = ctx->raw_sql.find('\'');
  if (pos == std::string::npos) {
    return Status::InvalidArgument("OPEN missing path");
  }
  ++pos;
  std::string path;
  while (pos < ctx->raw_sql.size() && ctx->raw_sql[pos] != '\'') {
    path.push_back(ctx->raw_sql[pos++]);
  }
  ctx->out->open.path = path;
  std::istringstream iss(ctx->raw_sql);
  std::string tok;
  while (iss >> tok) {
    const std::string u = Upper(tok);
    if (u == "BALANCED") ctx->out->open.durability = "balanced";
    else if (u == "SYNC") ctx->out->open.durability = "sync";
    else if (u == "GROUP") ctx->out->open.durability = "group";
    else if (u == "REQUIRE_PASS" || u == "ALLOW_WARN" || u == "MONITOR" ||
             u == "OFF") {
      ctx->out->open.attestation = ParseAttestation(tok);
    }
  }
  return Status::Ok();
}

Status ParseCreateIndex(ParseContext* ctx) {
  ctx->out->kind = QueryStmtKind::kCreateIndex;
  ctx->cursor.Consume(nullptr);
  if (ctx->cursor.PeekUpper() == "UNIQUE") {
    ctx->out->create_index.unique = true;
    ctx->cursor.Consume(nullptr);
  }
  if (ctx->cursor.PeekUpper() != "INDEX") {
    return Status::InvalidArgument("CREATE INDEX missing INDEX");
  }
  ctx->cursor.Consume(nullptr);
  ctx->cursor.Consume(&ctx->out->create_index.name);
  if (ctx->cursor.PeekUpper() != "ON") {
    return Status::InvalidArgument("CREATE INDEX missing ON");
  }
  ctx->cursor.Consume(nullptr);
  ctx->cursor.Consume(&ctx->out->create_index.table);
  if (ctx->cursor.Peek() != "(") {
    return Status::InvalidArgument("CREATE INDEX missing columns");
  }
  ctx->cursor.Consume(nullptr);
  while (!ctx->cursor.AtEnd() && ctx->cursor.Peek() != ")") {
    std::string col;
    ctx->cursor.Consume(&col);
    if (ctx->cursor.PeekUpper() == "DESC" || ctx->cursor.PeekUpper() == "ASC") {
      ctx->cursor.Consume(nullptr);
    }
    ctx->out->create_index.columns.push_back(col);
    if (ctx->cursor.Peek() == ",") ctx->cursor.Consume(nullptr);
  }
  if (ctx->cursor.Peek() == ")") ctx->cursor.Consume(nullptr);
  return Status::Ok();
}

Status ParseDropIndex(ParseContext* ctx) {
  ctx->out->kind = QueryStmtKind::kDropIndex;
  ctx->cursor.Consume(nullptr);
  if (ctx->cursor.PeekUpper() != "INDEX") {
    return Status::InvalidArgument("DROP missing INDEX");
  }
  ctx->cursor.Consume(nullptr);
  ctx->cursor.Consume(&ctx->out->drop_index);
  return Status::Ok();
}

Status ParseCreateTable(ParseContext* ctx) {
  ctx->out->kind = QueryStmtKind::kCreateTable;
  const auto lp = ctx->raw_sql.find('(');
  const auto rp = ctx->raw_sql.rfind(')');
  if (lp == std::string::npos || rp == std::string::npos) {
    return Status::InvalidArgument("CREATE TABLE parse failed");
  }
  std::istringstream head(ctx->raw_sql.substr(0, lp));
  std::string create, table_kw, table;
  head >> create >> table_kw >> table;
  ctx->out->create_table.table = table;
  const std::string cols = ctx->raw_sql.substr(lp + 1, rp - lp - 1);
  std::stringstream col_stream(cols);
  std::string col_line;
  bool first = true;
  while (std::getline(col_stream, col_line, ',')) {
    col_line = Trim(col_line);
    if (col_line.empty()) continue;
    ColumnDef cd{};
    std::istringstream cs(col_line);
    cs >> cd.name;
    std::string tok;
    while (cs >> tok) {
      const std::string u = Upper(tok);
      if (u == "TEXT" || u == "INTEGER" || u == "REAL") {
        cd.type = u;
      } else if (u == "FLOAT") {
        cd.type = "REAL";
      } else if (u == "VARCHAR") {
        cd.type = "TEXT";
      } else if (u == "NOT" && cs >> tok && Upper(tok) == "NULL") {
        cd.not_null = true;
      } else if (u == "PRIMARY" && cs >> tok && Upper(tok) == "KEY") {
        cd.primary_key = true;
        cd.not_null = true;
      }
    }
    std::string col_upper = col_line;
    for (char& c : col_upper) {
      c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    }
    const auto check_pos = col_upper.find("CHECK");
    if (check_pos != std::string::npos) {
      const auto p1 = col_line.find('(', check_pos);
      const auto p2 = col_line.rfind(')');
      if (p1 != std::string::npos && p2 != std::string::npos && p2 > p1) {
        cd.check_expr = Trim(col_line.substr(p1 + 1, p2 - p1 - 1));
      }
    }
    ctx->out->create_table.columns.push_back(cd);
    if (cd.primary_key) {
      ctx->out->create_table.key_column = cd.name;
      first = false;
    } else if (first) {
      ctx->out->create_table.key_column = cd.name;
      first = false;
    } else if (ctx->out->create_table.value_column.empty()) {
      ctx->out->create_table.value_column = cd.name;
    }
  }
  if (ctx->out->create_table.key_column.empty()) {
    ctx->out->create_table.key_column = "key";
    ctx->out->create_table.value_column = "value";
  }
  if (ctx->out->create_table.value_column.empty()) {
    ctx->out->create_table.value_column = ctx->out->create_table.key_column;
  }
  return Status::Ok();
}

Status ParseInsert(ParseContext* ctx) {
  ctx->cursor.Consume(nullptr);
  if (ctx->cursor.PeekUpper() == "OR") {
    ctx->cursor.Consume(nullptr);
    std::string action;
    ctx->cursor.Consume(&action);
    const std::string act = Upper(action);
    if (act == "REPLACE") {
      ctx->out->kind = QueryStmtKind::kUpsert;
    } else if (act == "IGNORE" || act == "ABORT" || act == "FAIL") {
      ctx->out->kind = QueryStmtKind::kInsert;
      ctx->out->insert.conflict_action = act;
    } else {
      return Status::InvalidArgument("unsupported INSERT OR");
    }
  } else {
    ctx->out->kind = QueryStmtKind::kInsert;
  }
  if (ctx->cursor.PeekUpper() != "INTO") {
    return Status::InvalidArgument("INSERT missing INTO");
  }
  ctx->cursor.Consume(nullptr);
  ctx->cursor.Consume(&ctx->out->insert.table);
  const Status cs = ParseColumnNameList(&ctx->cursor, &ctx->out->insert.column_names);
  if (!cs.ok()) return cs;

  if (ctx->cursor.PeekUpper() == "SELECT") {
    ctx->out->insert.select_sql = Trim(ctx->cursor.RestSql());
    return Status::Ok();
  }

  if (ctx->cursor.PeekUpper() != "VALUES") {
    return Status::InvalidArgument("INSERT missing VALUES or SELECT");
  }
  ctx->cursor.Consume(nullptr);
  const Status vs = ParseParenValueList(&ctx->cursor, &ctx->out->insert.values);
  if (!vs.ok()) return vs;
  FillLegacyInsertKeyValue(&ctx->out->insert);

  if (ctx->out->kind == QueryStmtKind::kUpsert) {
    ctx->out->upsert.table = ctx->out->insert.table;
    ctx->out->upsert.key = ctx->out->insert.key;
    ctx->out->upsert.value = ctx->out->insert.value;
    ctx->out->upsert.conflict_action = "REPLACE";
  }
  return Status::Ok();
}

std::unique_ptr<ExprNode> CloneExprNode(const ExprNode& node) {
  auto n = std::make_unique<ExprNode>();
  n->kind = node.kind;
  n->literal = node.literal;
  n->column = node.column;
  n->table = node.table;
  n->bin_op = node.bin_op;
  n->func_name = node.func_name;
  n->is_null_check = node.is_null_check;
  n->is_not = node.is_not;
  for (const auto& ch : node.children) {
    n->children.push_back(CloneExprNode(*ch));
  }
  return n;
}

Status ParseUpdate(ParseContext* ctx) {
  ctx->out->kind = QueryStmtKind::kUpdate;
  ctx->cursor.Consume(nullptr);
  ctx->cursor.Consume(&ctx->out->update.table);
  if (ctx->cursor.PeekUpper() != "SET") {
    return Status::InvalidArgument("UPDATE missing SET");
  }
  ctx->cursor.Consume(nullptr);
  ExprParse ep;
  std::vector<UpdateAssignment> assigns;
  std::unordered_map<std::string, size_t> col_index;
  while (!ctx->cursor.AtEnd() && ctx->cursor.PeekUpper() != "WHERE") {
    UpdateAssignment assign{};
    ctx->cursor.Consume(&assign.col);
    if (ctx->cursor.Peek() == "=") ctx->cursor.Consume(nullptr);
    const Status ss = ep.ParseAdd(&ctx->cursor, &assign.expr);
    if (!ss.ok()) return ss;
    const auto it = col_index.find(assign.col);
    if (it != col_index.end()) {
      assigns[it->second] = std::move(assign);
    } else {
      col_index[assign.col] = assigns.size();
      assigns.push_back(std::move(assign));
    }
    if (ctx->cursor.Peek() == ",") {
      ctx->cursor.Consume(nullptr);
      continue;
    }
    if (ctx->cursor.PeekUpper() == "WHERE") break;
    if (ctx->cursor.AtEnd()) break;
    return Status::InvalidArgument("expected , or WHERE in UPDATE SET");
  }
  if (assigns.empty()) {
    return Status::InvalidArgument("UPDATE missing assignments");
  }
  ctx->out->update.assignments = std::move(assigns);
  const auto& last = ctx->out->update.assignments.back();
  ctx->out->update.set_col = last.col;
  ctx->out->update.set_expr = CloneExprNode(*last.expr);
  if (last.expr->kind == ExprKind::kLiteral) {
    ctx->out->update.set_value = last.expr->literal;
  }
  if (ctx->cursor.PeekUpper() == "WHERE") {
    ctx->cursor.Consume(nullptr);
    ExprParse ep;
    std::unique_ptr<ExprNode> where;
    const Status ws = ep.ParsePredicate(&ctx->cursor, &where);
    if (!ws.ok()) return ws;
    ctx->out->update.where_expr = std::move(where);
    const ExprNode* w = ctx->out->update.where_expr.get();
    if (w && w->kind == ExprKind::kBinary &&
        w->bin_op == BinaryOp::kEq &&
        w->children[0]->kind == ExprKind::kColumn &&
        w->children[1]->kind == ExprKind::kLiteral) {
      ctx->out->update.where_col = w->children[0]->column;
      ctx->out->update.where_value = w->children[1]->literal;
    }
  }
  return Status::Ok();
}

Status ParseDelete(ParseContext* ctx) {
  ctx->out->kind = QueryStmtKind::kDelete;
  ctx->cursor.Consume(nullptr);
  if (ctx->cursor.PeekUpper() != "FROM") {
    return Status::InvalidArgument("DELETE missing FROM");
  }
  ctx->cursor.Consume(nullptr);
  ctx->cursor.Consume(&ctx->out->delete_stmt.table);
  if (ctx->cursor.PeekUpper() == "WHERE") {
    ctx->cursor.Consume(nullptr);
    ExprParse ep;
    std::unique_ptr<ExprNode> where;
    const Status ws = ep.ParsePredicate(&ctx->cursor, &where);
    if (!ws.ok()) return ws;
    ctx->out->delete_stmt.where_expr = std::move(where);
    const ExprNode* w = ctx->out->delete_stmt.where_expr.get();
    if (w && w->kind == ExprKind::kBinary &&
        w->bin_op == BinaryOp::kEq &&
        w->children[0]->kind == ExprKind::kColumn &&
        w->children[1]->kind == ExprKind::kLiteral) {
      ctx->out->delete_stmt.where_col = w->children[0]->column;
      ctx->out->delete_stmt.where_value = w->children[1]->literal;
    }
  }
  return Status::Ok();
}

Status ParseDropTable(ParseContext* ctx) {
  ctx->out->kind = QueryStmtKind::kDropTable;
  ctx->cursor.Consume(nullptr);
  if (ctx->cursor.PeekUpper() != "TABLE") {
    return Status::InvalidArgument("DROP missing TABLE");
  }
  ctx->cursor.Consume(nullptr);
  ctx->cursor.Consume(&ctx->out->drop_table);
  return Status::Ok();
}

Status ParseAlterTable(ParseContext* ctx) {
  ctx->out->kind = QueryStmtKind::kAlterTable;
  ctx->cursor.Consume(nullptr);
  if (ctx->cursor.PeekUpper() != "TABLE") {
    return Status::InvalidArgument("ALTER missing TABLE");
  }
  ctx->cursor.Consume(nullptr);
  ctx->cursor.Consume(&ctx->out->alter_table.table);
  std::string action;
  while (!ctx->cursor.AtEnd()) {
  std::string tok;
  ctx->cursor.Consume(&tok);
    if (!action.empty()) action.push_back(' ');
    action += tok;
  }
  ctx->out->alter_table.action = Trim(action);
  return Status::Ok();
}

Status ParseSelectStmt(ParseContext* ctx) {
  ctx->out->kind = QueryStmtKind::kSelect;
  SelectQuery sq{};
  SelectParse sp;
  const Status ps = sp.ParseSelect(ctx->raw_sql, &ctx->cursor, &sq);
  if (!ps.ok()) return ps;
  ctx->out->select_rich = std::move(sq);
  SyncLegacySelectFields(ctx->out);
  for (const auto& j : ctx->out->select_rich->joins) {
    JoinClause jc{};
    jc.table = j.table;
    jc.left_col = j.left_col;
    jc.right_col = j.right_col;
    jc.join_type = j.type == JoinType::kLeft ? "LEFT"
                 : j.type == JoinType::kRight ? "RIGHT"
                 : j.type == JoinType::kFull ? "FULL"
                 : j.type == JoinType::kCross ? "CROSS"
                 : "INNER";
    ctx->out->joins.push_back(jc);
  }
  return Status::Ok();
}

Status ParsePrepareQuery(const std::string& sql, QueryStatement* out) {
  if (!out) return Status::InvalidArgument("out is null");
  TokenCursor cur(TokenizeSql(sql));
  cur.Consume(nullptr);
  cur.Consume(&out->prepare.name);
  if (cur.PeekUpper() != "AS") {
    return Status::InvalidArgument("PREPARE missing AS");
  }
  cur.Consume(nullptr);
  out->prepare.sql = Trim(sql.substr(sql.find("AS") + 2));
  const auto semi = out->prepare.sql.find(';');
  if (semi != std::string::npos) {
    out->prepare.sql = Trim(out->prepare.sql.substr(0, semi));
  }
  out->kind = QueryStmtKind::kPrepare;
  out->raw_sql = sql;
  return Status::Ok();
}

Status ParseExecuteQuery(const std::string& sql, QueryStatement* out) {
  if (!out) return Status::InvalidArgument("out is null");
  TokenCursor cur(TokenizeSql(sql));
  cur.Consume(nullptr);
  cur.Consume(&out->execute.name);
  out->kind = QueryStmtKind::kExecute;
  out->raw_sql = sql;
  return Status::Ok();
}

Status ParseCreateViewQuery(const std::string& sql, QueryStatement* out) {
  if (!out) return Status::InvalidArgument("out is null");
  const auto as_pos = Upper(sql).find(" AS ");
  if (as_pos == std::string::npos) {
    return Status::InvalidArgument("CREATE VIEW missing AS");
  }
  TokenCursor cur(TokenizeSql(sql));
  cur.Consume(nullptr);
  if (cur.PeekUpper() == "TEMP" || cur.PeekUpper() == "TEMPORARY") {
    cur.Consume(nullptr);
  }
  if (cur.PeekUpper() != "VIEW") {
    return Status::InvalidArgument("CREATE VIEW missing VIEW");
  }
  cur.Consume(nullptr);
  cur.Consume(&out->create_view.name);
  std::string select_sql = Trim(sql.substr(as_pos + 4));
  const auto semi = select_sql.find(';');
  if (semi != std::string::npos) select_sql = select_sql.substr(0, semi);
  SelectQuery sq{};
  TokenCursor sc(TokenizeSql(select_sql));
  SelectParse sp;
  const Status ps = sp.ParseSelect(select_sql, &sc, &sq);
  if (!ps.ok()) return ps;
  out->create_view.base_table = sq.from_table;
  if (!sq.project_cols.empty()) out->create_view.key_column = sq.project_cols[0];
  if (sq.project_cols.size() > 1) out->create_view.value_column = sq.project_cols[1];
  else out->create_view.value_column = out->create_view.key_column;
  if (sq.where) {
    out->create_view.where_filter =
        std::shared_ptr<ExprNode>(CloneExprNode(*sq.where));
  }
  out->kind = QueryStmtKind::kCreateView;
  out->raw_sql = sql;
  return Status::Ok();
}

Status ParseDropViewQuery(const std::string& sql, QueryStatement* out) {
  if (!out) return Status::InvalidArgument("out is null");
  TokenCursor cur(TokenizeSql(sql));
  cur.Consume(nullptr);
  cur.Consume(nullptr);
  cur.Consume(&out->drop_view);
  out->kind = QueryStmtKind::kDropView;
  out->raw_sql = sql;
  return Status::Ok();
}

Status ParseCreateTriggerQuery(const std::string& sql, QueryStatement* out) {
  if (!out) return Status::InvalidArgument("out is null");
  TokenCursor cur(TokenizeSql(sql));
  cur.Consume(nullptr);
  cur.Consume(nullptr);
  cur.Consume(&out->create_trigger.name);
  if (cur.PeekUpper() == "AFTER") {
    cur.Consume(nullptr);
  }
  cur.Consume(&out->create_trigger.event);
  if (cur.PeekUpper() != "ON") {
    return Status::InvalidArgument("TRIGGER missing ON");
  }
  cur.Consume(nullptr);
  cur.Consume(&out->create_trigger.table);
  if (cur.PeekUpper() != "BEGIN") {
    return Status::InvalidArgument("TRIGGER missing BEGIN");
  }
  const auto begin = Upper(sql).find("BEGIN");
  const auto end = Upper(sql).rfind("END");
  if (begin == std::string::npos || end == std::string::npos) {
    return Status::InvalidArgument("TRIGGER missing body");
  }
  out->create_trigger.body_sql =
      Trim(sql.substr(begin + 5, end - begin - 5));
  out->create_trigger.event = Upper(out->create_trigger.event);
  out->kind = QueryStmtKind::kCreateTrigger;
  out->raw_sql = sql;
  return Status::Ok();
}

Status ParseDropTriggerQuery(const std::string& sql, QueryStatement* out) {
  if (!out) return Status::InvalidArgument("out is null");
  TokenCursor cur(TokenizeSql(sql));
  cur.Consume(nullptr);
  cur.Consume(nullptr);
  cur.Consume(&out->drop_trigger);
  out->kind = QueryStmtKind::kDropTrigger;
  out->raw_sql = sql;
  return Status::Ok();
}

Status ParseReindexQuery(const std::string& sql, QueryStatement* out) {
  if (!out) return Status::InvalidArgument("out is null");
  TokenCursor cur(TokenizeSql(sql));
  cur.Consume(nullptr);
  if (!cur.AtEnd()) {
    cur.Consume(&out->reindex_target);
  }
  out->kind = QueryStmtKind::kReindex;
  out->raw_sql = sql;
  return Status::Ok();
}

void SyncLegacySelectFields(QueryStatement* out) {
  if (!out || !out->select_rich) return;
  out->select.table = out->select_rich->from_table;
  out->select.max_pages = out->select_rich->max_pages;
  out->select.key.clear();
  if (out->select_rich->where &&
      out->select_rich->where->kind == ExprKind::kBinary &&
      out->select_rich->where->bin_op == BinaryOp::kEq &&
      out->select_rich->where->children[0]->kind == ExprKind::kColumn &&
      out->select_rich->where->children[1]->kind == ExprKind::kLiteral) {
    out->select.key = out->select_rich->where->children[1]->literal;
  }
}

void InstallNativeStmtRules(FirstMatchRegistry* registry) {
  if (!registry) return;
  registry->Register(
      {"open", 100, [](const ParseContext& c) { return c.cursor.PeekUpper() == "OPEN"; },
       ParseOpen});
  InstallDdlRules(registry);
  InstallDmlRules(registry);
}

}  // namespace parse
}  // namespace sql
}  // namespace ebtree
