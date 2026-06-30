#include "executor.h"

#include <unordered_map>

#include "ebtree/common/digest.h"
#include "sql/session/transaction_state.h"
#include "sql/catalog/row_codec.h"
#include "sql/exec/constraint_check.h"
#include "sql/exec/index_maintenance.h"
#include "sql/exec/mic_guard.h"

namespace ebtree {
namespace sql {

SqlExecutor::SqlExecutor(Engine* engine, Catalog* catalog,
                         OpLogWriter* op_log, DurabilityClass tier,
                         TransactionState* txn)
    : engine_(engine), catalog_(catalog), op_log_(op_log), tier_(tier),
      txn_(txn) {}

bool SqlExecutor::DurableAtReturn() const {
  return tier_ == DurabilityClass::kSync ||
         tier_ == DurabilityClass::kBalanced;
}

Status SqlExecutor::AppendOpLogPut(const std::string& encoded_key,
                                   const std::string& value) {
  if (!op_log_) return Status::Ok();
  const uint64_t lsn = engine_->stable_lsn();
  return op_log_->AppendPut(encoded_key, Sha256HexString(value), lsn,
                            DurableAtReturn(), tier_);
}

Status SqlExecutor::ExecCreateTable(const CreateTableStmt& stmt) {
  if (!stmt.columns.empty()) {
    return catalog_->CreateTable(stmt.table, stmt.columns, nullptr);
  }
  return catalog_->CreateTable(stmt.table, stmt.key_column,
                               stmt.value_column, nullptr);
}

namespace {

Status BuildInsertFields(Catalog* catalog, const TableSchema& table,
                         const InsertStmt& stmt,
                         std::unordered_map<std::string, std::string>* fields,
                         std::string* row_key) {
  if (!fields || !row_key) return Status::InvalidArgument("null out");
  fields->clear();
  if (!stmt.values.empty()) {
    std::vector<std::string> col_names = stmt.column_names;
    if (col_names.empty()) {
      for (const auto& c : table.columns) col_names.push_back(c.name);
    }
    if (stmt.values.size() != col_names.size()) {
      return Status::InvalidArgument("INSERT value count mismatch");
    }
    for (size_t i = 0; i < col_names.size(); ++i) {
      (*fields)[col_names[i]] = stmt.values[i];
    }
  } else {
    (*fields)[table.key_column] = stmt.key;
    (*fields)[table.value_column] = stmt.value;
  }
  for (const auto& c : table.columns) {
    if (c.primary_key) {
      const auto it = fields->find(c.name);
      if (it != fields->end()) {
        *row_key = it->second;
        return Status::Ok();
      }
    }
  }
  if (table.implicit_rowid && catalog) {
    return catalog->AllocateRowKey(table.name, row_key);
  }
  if (fields->count(table.key_column)) {
    *row_key = fields->at(table.key_column);
    return Status::Ok();
  }
  if (!fields->empty()) {
    *row_key = fields->begin()->second;
    return Status::Ok();
  }
  return Status::InvalidArgument("INSERT missing row key");
}

}  // namespace

Status SqlExecutor::ExecInsert(const InsertStmt& stmt) {
  if (catalog_->FindView(stmt.table)) {
    return Status::InvalidArgument("cannot modify view");
  }
  const auto table = catalog_->FindTable(stmt.table);
  if (!table) {
    return Status::InvalidArgument("unknown table: " + stmt.table);
  }
  std::unordered_map<std::string, std::string> fields;
  std::string row_key;
  const Status bs = BuildInsertFields(catalog_, *table, stmt, &fields, &row_key);
  if (!bs.ok()) return bs;

  std::string stored;
  if (table->schema_version >= 2) {
    const Status vs = ValidateRowConstraints(*table, fields);
    if (!vs.ok()) return vs;
    const Status es =
        EncodeRow(table->columns, table->schema_version, fields, &stored);
    if (!es.ok()) return es;
  } else {
    stored = fields.count(table->value_column) ? fields.at(table->value_column)
                                               : stmt.value;
  }
  const std::string encoded = catalog_->EncodeRowKey(table->id, row_key);
  if (txn_) txn_->RecordBeforePut(engine_, encoded);
  const Status st = engine_->Put(encoded, stored);
  if (!st.ok()) {
    return Status::Internal("engine put failed: " + st.message());
  }
  {
    std::unordered_map<std::string, std::string> index_fields = fields;
    if (table->schema_version >= 2) {
      (void)DecodeRowFields(stored, &index_fields);
    }
    const Status is = SyncIndexEntries(engine_, catalog_, *table, row_key,
                                       index_fields, false);
    if (!is.ok()) return is;
  }
  if (!op_log_) return Status::Ok();
  const Status ols = AppendOpLogPut(encoded, stored);
  if (!ols.ok()) {
    return Status::Internal("op_log append failed: " + ols.message());
  }
  return Status::Ok();
}

Status SqlExecutor::ExecSelect(const SelectStmt& stmt, ExecuteResult* out) {
  const auto table = catalog_->FindTable(stmt.table);
  if (!table) {
    return Status::InvalidArgument("unknown table: " + stmt.table);
  }

  const auto decode_value = [&](const std::string& raw) {
    std::string out_value = raw;
    if (table->schema_version >= 2) {
      std::unordered_map<std::string, std::string> fields;
      (void)DecodeRowFields(raw, &fields);
      if (fields.count(table->value_column)) {
        out_value = fields.at(table->value_column);
      }
    }
    return out_value;
  };

  TypedPlan plan{};
  const std::string prefix = std::to_string(table->id) + ":";
  if (stmt.key.empty()) {
    plan.op = PredicateOp::kRange;
    plan.key = prefix;
    plan.range_end = std::to_string(table->id + 1) + ":";

    const uint64_t pages_before = engine_->btree()->pages_touched();
    Status st = engine_->Prepare(plan);
    if (!st.ok()) return st;

    std::vector<std::pair<std::string, std::string>> rows;
    st = engine_->Scan(plan, &rows);
    if (!st.ok()) return st;

    st = CheckMicPagesBudget(engine_, pages_before, stmt.max_pages);
    if (!st.ok()) return st;
    if (stmt.max_pages.has_value() && rows.size() > *stmt.max_pages) {
      return Status::InvalidArgument(
          "MicContractViolation: row count " + std::to_string(rows.size()) +
          " exceeds max_pages " + std::to_string(*stmt.max_pages));
    }

    if (out) {
      for (const auto& row : rows) {
        if (Catalog::IsIndexEncodedKey(row.first)) continue;
        std::string user_key;
        if (!catalog_->DecodeRowKey(row.first, nullptr, &user_key)) {
          user_key = row.first;
        }
        out->rows.push_back(SqlRow{user_key, decode_value(row.second)});
      }
    }
    return Status::Ok();
  }

  const std::string encoded = catalog_->EncodeRowKey(table->id, stmt.key);
  const uint64_t pages_before = engine_->btree()->pages_touched();
  plan.op = PredicateOp::kEq;
  plan.key = encoded;
  Status st = engine_->Prepare(plan);
  if (!st.ok()) return st;

  std::string value;
  st = engine_->Get(encoded, &value);
  if (!st.ok()) {
    if (st.code() == StatusCode::kNotFound) return Status::Ok();
    return st;
  }

  st = CheckMicPagesBudget(engine_, pages_before, stmt.max_pages);
  if (!st.ok()) return st;

  if (out) {
    out->rows.push_back(SqlRow{stmt.key, decode_value(value)});
  }
  return Status::Ok();
}

Status SqlExecutor::Execute(const SqlStatement& stmt, ExecuteResult* out) {
  switch (stmt.kind) {
    case StmtKind::kCreateTable:
      return ExecCreateTable(stmt.create_table);
    case StmtKind::kInsert:
      return ExecInsert(stmt.insert);
    case StmtKind::kSelect:
      return ExecSelect(stmt.select, out);
    default:
      return Status::InvalidArgument("statement not executable");
  }
}

}  // namespace sql
}  // namespace ebtree
