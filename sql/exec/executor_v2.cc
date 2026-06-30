#include "executor_v2.h"

#include <sstream>
#include <unordered_map>

#include "sql/catalog/row_codec.h"
#include "sql/parse/shared/parse_shared.h"
#include "sql/exec/dml_executor.h"
#include "sql/exec/index_maintenance.h"
#include "sql/exec/mic_guard.h"
#include "sql/eval/expr_eval.h"
#include "sql/eval/schema_context.h"
#include "sql/exec/subquery_runner.h"
#include "sql/plan/lower.h"
#include "ebtree/common/digest.h"

namespace ebtree {
namespace sql {

namespace {

SqlStatement MakeSelectStmt(const SelectStmt& select) {
  SqlStatement stmt{};
  stmt.kind = StmtKind::kSelect;
  stmt.select = select;
  return stmt;
}

SqlStatement MakeCreateTableStmt(const CreateTableStmt& create) {
  SqlStatement stmt{};
  stmt.kind = StmtKind::kCreateTable;
  stmt.create_table = create;
  return stmt;
}

SqlStatement MakeInsertStmt(const InsertStmt& insert) {
  SqlStatement stmt{};
  stmt.kind = StmtKind::kInsert;
  stmt.insert = insert;
  return stmt;
}

}  // namespace

SqlExecutorV2::SqlExecutorV2(Engine* engine, Catalog* catalog,
                             CatalogStore* catalog_store, OpLogWriter* op_log,
                             DurabilityClass tier, TransactionState* txn)
    : engine_(engine),
      catalog_(catalog),
      catalog_store_(catalog_store),
      op_log_(op_log),
      tier_(tier),
      txn_(txn),
      base_(engine, catalog, op_log, tier, txn) {}

Status SqlExecutorV2::SaveCatalog() const {
  if (!catalog_store_) return Status::Ok();
  return catalog_store_->Save(*catalog_);
}

Status SqlExecutorV2::ExecCreateIndex(const QueryStatement& stmt) {
  const auto& ci = stmt.create_index;
  const auto table = catalog_->FindTable(ci.table);
  if (!table) return Status::InvalidArgument("unknown table: " + ci.table);
  const Status cs =
      catalog_->CreateIndex(ci.name, ci.table, ci.columns, ci.unique, nullptr);
  if (!cs.ok()) return cs;

  std::vector<std::pair<std::string, std::string>> rows;
  TypedPlan plan{};
  const std::string prefix = std::to_string(table->id) + ":";
  plan.op = PredicateOp::kRange;
  plan.key = prefix;
  plan.range_end = std::to_string(table->id + 1) + ":";
  const Status ps = engine_->Prepare(plan);
  if (!ps.ok()) return ps;
  const Status ss = engine_->Scan(plan, &rows);
  if (!ss.ok()) return ss;

  for (const auto& kv : rows) {
    std::string pk;
    if (!catalog_->DecodeRowKey(kv.first, nullptr, &pk)) continue;
    std::unordered_map<std::string, std::string> fields;
    (void)DecodeRowFields(kv.second, &fields);
    if (fields.empty()) {
      fields[table->key_column] = pk;
      fields[table->value_column] = kv.second;
    }
    const Status is =
        SyncIndexEntries(engine_, catalog_, *table, pk, fields, false);
    if (!is.ok()) return is;
  }
  return SaveCatalog();
}

Status SqlExecutorV2::ExecDropIndex(const std::string& name) {
  const auto idx = catalog_->FindIndex(name);
  if (!idx) return Status::InvalidArgument("unknown index: " + name);
  const auto table = catalog_->FindTable(idx->table);
  if (!table) return Status::InvalidArgument("unknown table for index");

  const std::string prefix =
      std::to_string(table->id) + ":i" + std::to_string(idx->id) + ":";
  TypedPlan plan{};
  plan.op = PredicateOp::kRange;
  plan.key = prefix;
  plan.range_end = prefix + "\xFF";
  const Status ps = engine_->Prepare(plan);
  if (!ps.ok()) return ps;
  std::vector<std::pair<std::string, std::string>> keys;
  const Status ss = engine_->Scan(plan, &keys);
  if (!ss.ok()) return ss;
  for (const auto& kv : keys) {
    const Status ds = engine_->Delete(kv.first);
    if (!ds.ok() && ds.code() != StatusCode::kNotFound) return ds;
  }
  const Status st = catalog_->DropIndex(name);
  if (!st.ok()) return st;
  return SaveCatalog();
}

Status SqlExecutorV2::TryExecIndexScanSelect(const QueryStatement& stmt,
                                             ExecuteResult* out,
                                             bool* handled) {
  if (handled) *handled = false;
  if (!stmt.select_rich || !stmt.select_rich->where) return Status::Ok();
  const ExprNode* w = stmt.select_rich->where.get();
  if (w->kind != ExprKind::kBinary || w->bin_op != BinaryOp::kEq ||
      w->children.size() != 2 || w->children[0]->kind != ExprKind::kColumn ||
      w->children[1]->kind != ExprKind::kLiteral) {
    return Status::Ok();
  }
  const std::string col = w->children[0]->column;
  const std::string val = w->children[1]->literal;
  const auto table = catalog_->FindTable(stmt.select_rich->from_table);
  if (!table) return Status::InvalidArgument("unknown table");

  const IndexDef* use_idx = nullptr;
  for (const auto& idx : catalog_->IndexesForTable(table->name)) {
    if (!idx.columns.empty() && idx.columns[0] == col) {
      use_idx = &idx;
      break;
    }
  }
  if (!use_idx) return Status::Ok();
  if (handled) *handled = true;

  const std::string prefix = std::to_string(table->id) + ":i" +
                             std::to_string(use_idx->id) + ":" + val + ":";
  TypedPlan plan{};
  plan.op = PredicateOp::kRange;
  plan.key = prefix;
  plan.range_end = prefix + "\xFF";
  const uint64_t pages_before = engine_->btree()->pages_touched();
  const Status ps = engine_->Prepare(plan);
  if (!ps.ok()) return ps;
  std::vector<std::pair<std::string, std::string>> index_rows;
  const Status ss = engine_->Scan(plan, &index_rows);
  if (!ss.ok()) return ss;

  if (out) {
    for (const auto& kv : index_rows) {
      std::string pk;
      if (!catalog_->DecodeIndexKeyPrefix(kv.first, nullptr, nullptr, nullptr,
                                           &pk)) {
        pk = kv.second;
      }
      const std::string encoded = catalog_->EncodeRowKey(table->id, pk);
      std::string raw;
      const Status gs = engine_->Get(encoded, &raw);
      if (!gs.ok()) continue;
      std::unordered_map<std::string, std::string> fields;
      (void)DecodeRowFields(raw, &fields);
      std::string out_val = raw;
      if (fields.count(table->value_column)) {
        out_val = fields.at(table->value_column);
      }
      out->rows.push_back(SqlRow{pk, out_val});
    }
  }

  const Status mic = CheckMicPagesBudget(
      engine_, pages_before, stmt.select_rich->max_pages);
  if (!mic.ok()) return mic;
  return Status::Ok();
}

Status SqlExecutorV2::ExecDropTable(const std::string& table) {
  const Status st = catalog_->DropTable(table);
  if (!st.ok()) return st;
  return SaveCatalog();
}

Status SqlExecutorV2::ExecAlterTable(const QueryStatement& stmt) {
  if (!catalog_->FindTable(stmt.alter_table.table)) {
    return Status::InvalidArgument("unknown table: " + stmt.alter_table.table);
  }
  return SaveCatalog();
}

Status SqlExecutorV2::ExecUpdate(const QueryStatement& stmt) {
  DmlExecutor dml(engine_, catalog_, catalog_store_, op_log_, tier_, txn_);
  return dml.ExecUpdate(stmt);
}

Status SqlExecutorV2::ExecDelete(const QueryStatement& stmt) {
  if (catalog_->FindView(stmt.delete_stmt.table)) {
    return Status::InvalidArgument("cannot modify view");
  }
  const auto table = catalog_->FindTable(stmt.delete_stmt.table);
  if (!table) {
    return Status::InvalidArgument("unknown table: " + stmt.delete_stmt.table);
  }
  if (!stmt.delete_stmt.where_value) {
    return Status::InvalidArgument("DELETE requires WHERE");
  }
  const std::string encoded =
      catalog_->EncodeRowKey(table->id, *stmt.delete_stmt.where_value);
  if (txn_) txn_->RecordBeforeDelete(engine_, encoded);
  std::string existing;
  std::unordered_map<std::string, std::string> fields;
  if (const Status gs = engine_->Get(encoded, &existing); gs.ok()) {
    (void)DecodeRowFields(existing, &fields);
  }
  const Status st = engine_->Delete(encoded);
  if (!st.ok()) return st;
  if (!fields.empty()) {
    const Status is = SyncIndexEntries(engine_, catalog_, *table,
                                       *stmt.delete_stmt.where_value, fields,
                                       true);
    if (!is.ok()) return is;
  }
  if (op_log_) {
    const bool durable = tier_ == DurabilityClass::kSync ||
                         tier_ == DurabilityClass::kBalanced;
    return op_log_->AppendDelete(encoded, engine_->stable_lsn(), durable, tier_);
  }
  return Status::Ok();
}

Status SqlExecutorV2::ExecSelectJoin(const QueryStatement& stmt,
                                     ExecuteResult* out) {
  if (stmt.joins.empty()) {
    return base_.Execute(MakeSelectStmt(stmt.select), out);
  }

  const auto left = catalog_->FindTable(stmt.select.table);
  if (!left) {
    return Status::InvalidArgument("unknown table: " + stmt.select.table);
  }
  const auto& join = stmt.joins[0];
  const auto right = catalog_->FindTable(join.table);
  if (!right) {
    return Status::InvalidArgument("unknown table: " + join.table);
  }

  ExecuteResult left_rows{};
  SelectStmt left_scan = stmt.select;
  left_scan.key.clear();
  const Status ls = base_.Execute(MakeSelectStmt(left_scan), &left_rows);
  if (!ls.ok()) return ls;

  ExecuteResult right_rows{};
  SelectStmt right_scan{};
  right_scan.table = join.table;
  const Status rs = base_.Execute(MakeSelectStmt(right_scan), &right_rows);
  if (!rs.ok()) return rs;

  if (out) {
    for (const auto& l : left_rows.rows) {
      for (const auto& r : right_rows.rows) {
        if (l.key == r.key) {
          SqlRow row{};
          row.key = l.key;
          row.value = l.value + "|" + r.value;
          out->rows.push_back(row);
        }
      }
    }
  }
  return Status::Ok();
}

Status SqlExecutorV2::ExecInsertSelect(const QueryStatement& stmt,
                                       ExecuteResult* out) {
  (void)out;
  const auto dest = catalog_->FindTable(stmt.insert.table);
  if (!dest) {
    return Status::InvalidArgument("unknown table: " + stmt.insert.table);
  }
  const std::string sql = parse::Trim(stmt.insert.select_sql);
  std::istringstream iss(sql);
  std::string sel, proj, from, src_name;
  iss >> sel >> proj >> from >> src_name;
  if (parse::Upper(sel) != "SELECT" || parse::Upper(from) != "FROM" || src_name.empty()) {
    return Status::InvalidArgument("unsupported INSERT SELECT: " + sql);
  }
  const auto src = catalog_->FindTable(src_name);
  if (!src) {
    return Status::InvalidArgument("unknown source table: " + src_name);
  }

  TypedPlan plan{};
  const std::string prefix = std::to_string(src->id) + ":";
  plan.op = PredicateOp::kRange;
  plan.key = prefix;
  plan.range_end = std::to_string(src->id + 1) + ":";
  const Status ps = engine_->Prepare(plan);
  if (!ps.ok()) return ps;
  std::vector<std::pair<std::string, std::string>> rows;
  const Status ss = engine_->Scan(plan, &rows);
  if (!ss.ok()) return ss;

  for (const auto& kv : rows) {
    std::unordered_map<std::string, std::string> fields;
    (void)DecodeRowFields(kv.second, &fields);
    if (fields.empty()) {
      std::string pk;
      if (!catalog_->DecodeRowKey(kv.first, nullptr, &pk)) pk = kv.first;
      fields[src->key_column] = pk;
      fields[src->value_column] = kv.second;
    }
    InsertStmt ins{};
    ins.table = stmt.insert.table;
    if (!stmt.insert.column_names.empty()) {
      ins.column_names = stmt.insert.column_names;
      for (const auto& col : ins.column_names) {
        ins.values.push_back(fields.count(col) ? fields.at(col) : "");
      }
    } else {
      for (const auto& col : dest->columns) {
        ins.values.push_back(fields.count(col.name) ? fields.at(col.name) : "");
      }
    }
    const Status is = base_.Execute(MakeInsertStmt(ins), out);
    if (!is.ok()) return is;
  }
  return Status::Ok();
}

Status SqlExecutorV2::ExecUpsert(const QueryStatement& stmt, ExecuteResult* out) {
  (void)out;
  InsertStmt ins = stmt.insert;
  if (!stmt.upsert.table.empty()) ins.table = stmt.upsert.table;
  if (!stmt.upsert.key.empty()) {
    ins.key = stmt.upsert.key;
    ins.value = stmt.upsert.value;
  }
  return base_.Execute(MakeInsertStmt(ins), out);
}

Status SqlExecutorV2::Execute(const QueryStatement& stmt, ExecuteResult* out) {
  plan::LoweredPlan lowered{};
  const Status ls = plan::LowerQuery(stmt, &lowered);
  if (!ls.ok()) return ls;
  (void)lowered;

  switch (stmt.kind) {
    case QueryStmtKind::kCreateTable: {
      const Status st = base_.Execute(MakeCreateTableStmt(stmt.create_table), out);
      if (!st.ok()) return st;
      return SaveCatalog();
    }
    case QueryStmtKind::kUpsert:
      return ExecUpsert(stmt, out);
    case QueryStmtKind::kCreateIndex:
      return ExecCreateIndex(stmt);
    case QueryStmtKind::kDropIndex:
      return ExecDropIndex(stmt.drop_index);
    case QueryStmtKind::kInsert:
      if (!stmt.insert.select_sql.empty()) {
        return ExecInsertSelect(stmt, out);
      }
      return base_.Execute(MakeInsertStmt(stmt.insert), out);
    case QueryStmtKind::kSelect: {
      bool handled = false;
      const Status idx_st = TryExecIndexScanSelect(stmt, out, &handled);
      if (!idx_st.ok()) return idx_st;
      if (handled) return Status::Ok();
      if (!stmt.joins.empty()) return ExecSelectJoin(stmt, out);
      return base_.Execute(MakeSelectStmt(stmt.select), out);
    }
    case QueryStmtKind::kDropTable:
      return ExecDropTable(stmt.drop_table);
    case QueryStmtKind::kAlterTable:
      return ExecAlterTable(stmt);
    case QueryStmtKind::kUpdate:
      return ExecUpdate(stmt);
    case QueryStmtKind::kDelete:
      return ExecDelete(stmt);
    default:
      return Status::InvalidArgument("unsupported executable statement");
  }
}

}  // namespace sql
}  // namespace ebtree
