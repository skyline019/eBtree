#include "dml_executor.h"

#include "sql/catalog/row_codec.h"
#include "sql/eval/schema_context.h"
#include "sql/exec/index_maintenance.h"
#include "sql/exec/constraint_engine.h"
#include "sql/exec/txn_read_policy.h"
#include "ebtree/common/digest.h"

namespace ebtree {
namespace sql {

namespace {

std::unique_ptr<ExprNode> CloneExprShallowTree(const ExprNode& node) {
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
    n->children.push_back(CloneExprShallowTree(*ch));
  }
  return n;
}

}  // namespace

DmlExecutor::DmlExecutor(ebtree::Engine* engine, Catalog* catalog,
                         CatalogStore* catalog_store, OpLogWriter* op_log,
                         DurabilityClass tier, TransactionState* txn)
    : engine_(engine),
      catalog_(catalog),
      catalog_store_(catalog_store),
      op_log_(op_log),
      tier_(tier),
      txn_(txn),
      scan_(engine, catalog, txn) {}

Status DmlExecutor::SaveCatalog() const {
  if (!catalog_store_) return Status::Ok();
  return catalog_store_->Save(*catalog_);
}

std::string DmlExecutor::ResolveSetValue(const ExprNode& expr,
                                         const RowMap& row,
                                         const TableSchema& table) const {
  ExprEval eval;
  SchemaContext ctx;
  ctx.table = &table;
  eval.SetSchemaContext(ctx);
  SubqueryRunner sub_runner(engine_, catalog_);
  eval.SetSubqueryEval([&sub_runner](const ExprNode& node, const RowMap& outer) {
    return sub_runner.EvalSubqueryTruth(node, outer, 0);
  });
  return eval.EvalScalar(expr, row);
}

static bool TableHasColumn(const TableSchema& table, const std::string& col) {
  for (const auto& c : table.columns) {
    if (c.name == col) return true;
  }
  return false;
}

Status DmlExecutor::ExecUpdate(const QueryStatement& stmt) {
  if (catalog_->FindView(stmt.update.table)) {
    return Status::InvalidArgument("cannot modify view");
  }
  const auto table = catalog_->FindTable(stmt.update.table);
  if (!table) return Status::InvalidArgument("unknown table: " + stmt.update.table);

  std::vector<UpdateAssignment> fallback;
  const std::vector<UpdateAssignment>* assignments = &stmt.update.assignments;
  if (assignments->empty()) {
    UpdateAssignment one{};
    one.col = stmt.update.set_col;
    if (stmt.update.set_expr) {
      one.expr = CloneExprShallowTree(*stmt.update.set_expr);
    } else {
      auto lit = std::make_unique<ExprNode>();
      lit->kind = ExprKind::kLiteral;
      lit->literal = stmt.update.set_value;
      one.expr = std::move(lit);
    }
    fallback.push_back(std::move(one));
    assignments = &fallback;
  }
  for (const auto& assign : *assignments) {
    if (!TableHasColumn(*table, assign.col)) {
      return Status::InvalidArgument("unknown column: " + assign.col);
    }
  }

  const bool use_point_pk =
      stmt.update.where_col.has_value() &&
      stmt.update.where_value.has_value() && !table->implicit_rowid &&
      *stmt.update.where_col == table->key_column;

  const bool use_scan =
      !use_point_pk &&
      (stmt.update.where_expr || !stmt.update.where_col.has_value() ||
       table->implicit_rowid ||
       (stmt.update.where_col.has_value() &&
        *stmt.update.where_col != table->key_column));

  if (use_scan) {
    std::vector<std::pair<std::string, std::string>> encoded;
    const Status ss = scan_.ScanTable(*table, &encoded);
    if (!ss.ok()) return ss;

    SubqueryRunner sub_runner(engine_, catalog_);
    sub_runner.ClearLastError();
    ExprEval eval;
    SchemaContext ctx;
    ctx.table = &(*table);
    eval.SetSchemaContext(ctx);
    sub_runner.SetSubqueryEval([&sub_runner](const ExprNode& node, const RowMap& outer) {
      return sub_runner.EvalSubqueryTruth(node, outer, 0);
    });
    eval.SetSubqueryEval([&sub_runner](const ExprNode& node, const RowMap& outer) {
      return sub_runner.EvalSubqueryTruth(node, outer, 0);
    });

    for (const auto& kv : encoded) {
      std::string user_key;
      if (!catalog_->DecodeRowKey(kv.first, nullptr, &user_key)) user_key = kv.first;
      RowMap fields;
      if (table->schema_version >= 2) {
        (void)DecodeRowFields(kv.second, &fields);
      }
      if (!fields.count(table->key_column)) {
        fields[table->key_column] = user_key;
      }
      if (!fields.count(table->value_column)) {
        fields[table->value_column] = kv.second;
      }
      if (stmt.update.where_expr && !eval.EvalBool(*stmt.update.where_expr, fields)) {
        if (!sub_runner.LastError().ok()) return sub_runner.LastError();
        continue;
      }
      std::string stored = kv.second;
      RowMap working = fields;
      for (const auto& assign : *assignments) {
        const std::string set_val =
            ResolveSetValue(*assign.expr, working, *table);
        if (table->schema_version >= 2) {
          const Status us = UpdateRowFields(stored, assign.col, set_val, &stored);
          if (!us.ok()) return us;
        } else if (assign.col == table->value_column) {
          stored = set_val;
        }
        working[assign.col] = set_val;
      }
      {
        const Status cst = ValidateRowConstraintsWithEval(*table, working, &eval);
        if (!cst.ok()) return cst;
      }
      if (txn_) {
        txn_->RecordBeforePut(engine_, kv.first);
        txn_->RecordWriteIntent(engine_, kv.first);
      }
      const Status put_st =
          engine_->Put(kv.first, stored, txn_ && txn_->active() ? txn_->txn_id() : 0);
      if (!put_st.ok()) return put_st;
      std::unordered_map<std::string, std::string> index_fields;
      (void)DecodeRowFields(stored, &index_fields);
      if (index_fields.empty()) {
        index_fields = working;
      }
      const Status is =
          SyncIndexEntries(engine_, catalog_, *table, user_key, index_fields,
                           false, txn_ && txn_->active() ? txn_->txn_id() : 0);
      if (!is.ok()) return is;
      if (op_log_) {
        const bool durable = tier_ == DurabilityClass::kSync ||
                             tier_ == DurabilityClass::kBalanced;
        const Status ols = op_log_->AppendPut(
            kv.first, Sha256HexString(stored), engine_->stable_lsn(), durable, tier_);
        if (!ols.ok()) return ols;
      }
    }
    return Status::Ok();
  }

  if (!stmt.update.where_col.has_value() || !stmt.update.where_value.has_value()) {
    return Status::InvalidArgument("UPDATE requires WHERE key = '...'");
  }
  const std::string encoded =
      catalog_->EncodeRowKey(table->id, *stmt.update.where_value);
  RowMap point_row;
  point_row[table->key_column] = *stmt.update.where_value;
  std::string stored;
  if (table->schema_version >= 2) {
    const Status gs = TxnGet(engine_, txn_, encoded, &stored);
    if (!gs.ok() && gs.code() != StatusCode::kNotFound) return gs;
  }
  if (txn_) txn_->RecordWriteIntent(engine_, encoded);
  RowMap working = point_row;
  for (const auto& assign : *assignments) {
    const std::string set_val =
        ResolveSetValue(*assign.expr, working, *table);
    if (table->schema_version >= 2) {
      if (stored.empty()) {
        std::unordered_map<std::string, std::string> values;
        values[table->key_column] = *stmt.update.where_value;
        values[assign.col] = set_val;
        const Status es =
            EncodeRow(table->columns, table->schema_version, values, &stored);
        if (!es.ok()) return es;
      } else {
        const Status us = UpdateRowFields(stored, assign.col, set_val, &stored);
        if (!us.ok()) return us;
      }
    } else {
      if (assign.col != table->value_column) {
        return Status::InvalidArgument("legacy table supports value column only");
      }
      stored = set_val;
    }
    working[assign.col] = set_val;
  }
  {
    ExprEval eval;
    SchemaContext ctx;
    ctx.table = &(*table);
    eval.SetSchemaContext(ctx);
    const Status cst = ValidateRowConstraintsWithEval(*table, working, &eval);
    if (!cst.ok()) return cst;
  }
  if (txn_) txn_->RecordBeforePut(engine_, encoded);
  const Status put_st =
      engine_->Put(encoded, stored, txn_ && txn_->active() ? txn_->txn_id() : 0);
  if (!put_st.ok()) return put_st;
  std::unordered_map<std::string, std::string> fields;
  if (table->schema_version >= 2) {
    (void)DecodeRowFields(stored, &fields);
  } else {
    fields = working;
  }
  const Status is =
      SyncIndexEntries(engine_, catalog_, *table, *stmt.update.where_value,
                       fields, false,
                       txn_ && txn_->active() ? txn_->txn_id() : 0);
  if (!is.ok()) return is;
  if (op_log_) {
    const bool durable = tier_ == DurabilityClass::kSync ||
                         tier_ == DurabilityClass::kBalanced;
    const Status ols = op_log_->AppendPut(
        encoded, Sha256HexString(stored), engine_->stable_lsn(), durable, tier_);
    if (!ols.ok()) return ols;
  }
  return Status::Ok();
}

}  // namespace sql
}  // namespace ebtree
