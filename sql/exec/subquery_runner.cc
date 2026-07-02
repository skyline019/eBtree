#include "subquery_runner.h"

#include <algorithm>

#include "sql/catalog/catalog.h"
#include "sql/catalog/row_codec.h"
#include "sql/eval/expr_eval.h"
#include "sql/eval/truth_value.h"
#include "sql/eval/type_affinity.h"
#include "sql/exec/cte_context.h"
#include "sql/exec/mic_guard.h"
#include "sql/exec/physical_scan.h"
#include "ebtree/engine/engine.h"

namespace ebtree {
namespace sql {
namespace {

constexpr int kMaxSubqueryDepth = 3;
constexpr size_t kDefaultMaxSubqueryRows = 4096;

std::string FieldVal(const RowMap& r, const std::string& col) {
  if (r.count(col)) return r.at(col);
  const auto dot = col.find('.');
  if (dot != std::string::npos) {
    const std::string bare = col.substr(dot + 1);
    if (r.count(bare)) return r.at(bare);
  }
  if (r.count("value")) return r.at("value");
  if (r.count("key")) return r.at("key");
  return "";
}

Status CheckRowBudget(size_t count, std::optional<uint64_t> max_pages) {
  if (max_pages.has_value()) {
    return CheckMicRowBudget(count, max_pages);
  }
  if (count > kDefaultMaxSubqueryRows) {
    return Status::InvalidArgument("subquery scan too large");
  }
  return Status::Ok();
}

bool IsSimpleCorrelatedEq(const ExprNode& node) {
  return node.kind == ExprKind::kBinary && node.bin_op == BinaryOp::kEq &&
         node.children.size() == 2 &&
         node.children[0]->kind == ExprKind::kColumn &&
         node.children[1]->kind == ExprKind::kColumn;
}

bool EvalCorrelatedEq(const ExprNode& where, const RowMap& row) {
  ExprEval eval;
  const std::string l = eval.EvalScalar(*where.children[0], row);
  const std::string r = eval.EvalScalar(*where.children[1], row);
  return !l.empty() && l == r;
}

Status ValidateSubqueryDepth(const ExprNode& node, int parent_depth) {
  if (node.kind == ExprKind::kSubquery && node.subquery.has_value()) {
    const int level = parent_depth + 1;
    if (level >= kMaxSubqueryDepth) {
      return Status::InvalidArgument("subquery depth exceeded");
    }
    const SubquerySpec& sq = *node.subquery;
    if (sq.parsed_query && sq.parsed_query->where) {
      const Status st =
          ValidateSubqueryDepth(*sq.parsed_query->where, level);
      if (!st.ok()) return st;
    }
  }
  for (const auto& c : node.children) {
    const Status st = ValidateSubqueryDepth(*c, parent_depth);
    if (!st.ok()) return st;
  }
  return Status::Ok();
}

}  // namespace

int SubqueryResultColumnCount(const SelectQuery& query, const Catalog* catalog) {
  if (!query.scalar_projects.empty()) {
    return static_cast<int>(query.scalar_projects.size());
  }
  if (!query.aggregates.empty()) {
    return static_cast<int>(query.aggregates.size());
  }
  if (!query.project_cols.empty()) {
    if (query.project_cols[0] == "*") {
      if (!query.from_table.empty() && catalog) {
        const auto table = catalog->FindTable(query.from_table);
        if (table) return static_cast<int>(table->columns.size());
      }
      return 1;
    }
    return static_cast<int>(query.project_cols.size());
  }
  return 1;
}

Status SubqueryRunner::ValidateWhereDepth(const ExprNode& where,
                                          int parent_depth) {
  return ValidateSubqueryDepth(where, parent_depth);
}

SubqueryRunner::SubqueryRunner(ebtree::Engine* engine, Catalog* catalog)
    : engine_(engine), catalog_(catalog) {}

Status SubqueryRunner::ScanTable(
    const TableSchema& table,
    std::vector<std::pair<std::string, std::string>>* rows) const {
  PhysicalScan scan(engine_, catalog_);
  return scan.ScanTable(table, rows);
}

void SubqueryRunner::PopulateFields(const TableSchema& table,
                                    const std::string& user_key,
                                    const std::string& raw,
                                    RowMap* fields) const {
  fields->clear();
  if (table.schema_version >= 2) {
    (void)DecodeRowFields(raw, fields);
  }
  if (table.schema_version < 2 &&
      (fields->empty() || !fields->count(table.value_column))) {
    fields->clear();
    if (!table.implicit_rowid) {
      (*fields)[table.key_column] = user_key;
    }
    (*fields)[table.value_column] = raw;
  }
  (*fields)["key"] = user_key;
  for (const auto& kv : *fields) {
    (*fields)[table.name + "." + kv.first] = kv.second;
  }
}

bool SubqueryRunner::EvalWhere(const ExprNode& node, const RowMap& row,
                               int level) const {
  if (level >= kMaxSubqueryDepth) return false;
  ExprEval eval;
  eval.SetSubqueryEval([this, level](const ExprNode& n, const RowMap& r) {
    return EvalSubqueryTruth(n, r, level);
  });
  return eval.EvalBool(node, row);
}

bool SubqueryRunner::ExistsMatch(const SelectQuery& query,
                                 const RowMap& outer_row, int level) const {
  if (level >= kMaxSubqueryDepth) return false;

  if (level >= 2 && query.where && IsSimpleCorrelatedEq(*query.where)) {
    return EvalCorrelatedEq(*query.where, outer_row);
  }

  const auto table = catalog_->FindTable(query.from_table);
  if (table) {
    std::vector<std::pair<std::string, std::string>> encoded;
    if (!ScanTable(*table, &encoded).ok()) return false;
    const Status budget = CheckRowBudget(encoded.size(), max_pages_);
    if (!budget.ok()) return false;

    for (const auto& kv : encoded) {
      std::string user_key;
      if (!catalog_->DecodeRowKey(kv.first, nullptr, &user_key)) user_key = kv.first;
      RowMap fields;
      PopulateFields(*table, user_key, kv.second, &fields);
      RowMap eval_fields = fields;
      for (const auto& o : outer_row) {
        if (o.first.find('.') != std::string::npos) {
          eval_fields[o.first] = o.second;
        }
      }
      if (!query.where || EvalWhere(*query.where, eval_fields, level)) {
        return true;
      }
    }
    return false;
  }

  if (cte_ctx_ && cte_ctx_->Has(query.from_table)) {
    const std::vector<RowMap>* cte_rows = cte_ctx_->Get(query.from_table);
    if (!cte_rows) return false;
    const Status budget = CheckRowBudget(cte_rows->size(), max_pages_);
    if (!budget.ok()) return false;
    for (const auto& fields : *cte_rows) {
      RowMap eval_fields = fields;
      for (const auto& o : outer_row) {
        if (o.first.find('.') != std::string::npos) {
          eval_fields[o.first] = o.second;
        }
      }
      if (!query.where || EvalWhere(*query.where, eval_fields, level)) {
        return true;
      }
    }
    return false;
  }

  return false;
}

std::string SubqueryRowProjectValue(const RowMap& row, const SelectQuery& query) {
  if (!query.project_cols.empty() && query.project_cols[0] != "*") {
    return FieldVal(row, query.project_cols[0]);
  }
  for (const auto& kv : row) {
    if (kv.first == "key" || kv.first == "value") continue;
    const auto dot = kv.first.find('.');
    if (dot != std::string::npos && kv.first.rfind('.', dot) != std::string::npos) {
      continue;
    }
    return kv.second;
  }
  return FieldVal(row, "key");
}

TruthValue SubqueryRunner::EvalSubqueryTruth(const ExprNode& node,
                                             const RowMap& outer,
                                             int parent_depth) const {
  if (parent_depth >= kMaxSubqueryDepth) {
    last_error_ = Status::InvalidArgument("subquery depth exceeded");
    return TruthValue::kFalse;
  }
  if (!node.subquery.has_value() || !node.subquery->parsed_query) {
    return TruthValue::kFalse;
  }
  const SubquerySpec& spec = *node.subquery;
  const int level = parent_depth + 1;

  if (spec.lhs && spec.in_literals.empty() && !spec.exists) {
    const int cols = SubqueryResultColumnCount(*spec.parsed_query, catalog_);
    if (cols > 1) {
      last_error_ = Status::InvalidArgument("sub-select returns columns");
      return TruthValue::kFalse;
    }
  }

  if (spec.exists) {
    return ExistsMatch(*spec.parsed_query, outer, level) ? TruthValue::kTrue
                                                          : TruthValue::kFalse;
  }

  std::vector<RowMap> results;
  const Status run_st = Run(*spec.parsed_query, outer, &results, level);
  if (!run_st.ok()) {
    last_error_ = run_st;
    return TruthValue::kFalse;
  }

  if (spec.lhs) {
    ExprEval scalar_eval;
    scalar_eval.SetSubqueryEval([this, level](const ExprNode& n, const RowMap& r) {
      return EvalSubqueryTruth(n, r, level);
    });
    const SqlValue lhs = scalar_eval.EvalValue(*spec.lhs, outer);
    if (!spec.in_literals.empty()) {
      ExprEval in_eval;
      return in_eval.EvalInLiterals(lhs, spec.in_literals, spec.not_op);
    }
    if (results.empty()) {
      return spec.not_op ? TruthValue::kTrue : TruthValue::kFalse;
    }
    if (lhs.IsNull()) return TruthValue::kUnknown;
    const SelectQuery& inner = *spec.parsed_query;
    bool found = false;
    bool seen_null = false;
    for (const auto& r : results) {
      const std::string v = SubqueryRowProjectValue(r, inner);
      const SqlValue rv = SqlValue::FromLegacyString(v);
      if (rv.IsNull()) {
        seen_null = true;
        continue;
      }
      if (IsSqlTrue(CompareSqlValues(lhs, rv, BinaryOp::kEq, TypeAffinity::kText))) {
        found = true;
      }
    }
    TruthValue tv = TruthValue::kFalse;
    if (found) {
      tv = TruthValue::kTrue;
    } else if (seen_null) {
      tv = TruthValue::kUnknown;
    }
    return spec.not_op ? Not3(tv) : tv;
  }
  return results.empty() ? TruthValue::kFalse : TruthValue::kTrue;
}

Status SubqueryRunner::Run(const SelectQuery& query, const RowMap& outer_row,
                           std::vector<RowMap>* out, int level) const {
  if (level >= kMaxSubqueryDepth) {
    return Status::InvalidArgument("subquery depth exceeded");
  }
  if (!out) return Status::InvalidArgument("out is null");
  out->clear();

  const auto table = catalog_->FindTable(query.from_table);
  if (table) {
    std::vector<std::pair<std::string, std::string>> encoded;
    const Status ss = ScanTable(*table, &encoded);
    if (!ss.ok()) return ss;
    const Status budget = CheckRowBudget(encoded.size(), max_pages_);
    if (!budget.ok()) return budget;

    for (const auto& kv : encoded) {
      std::string user_key;
      if (!catalog_->DecodeRowKey(kv.first, nullptr, &user_key)) user_key = kv.first;
      RowMap fields;
      PopulateFields(*table, user_key, kv.second, &fields);
      RowMap eval_fields = fields;
      for (const auto& o : outer_row) {
        if (o.first.find('.') != std::string::npos) {
          eval_fields[o.first] = o.second;
        }
      }
      if (query.where && !EvalWhere(*query.where, eval_fields, level)) {
        if (!last_error_.ok()) return last_error_;
        continue;
      }
      out->push_back(fields);
      const Status out_budget = CheckRowBudget(out->size(), max_pages_);
      if (!out_budget.ok()) return out_budget;
    }
    return Status::Ok();
  }

  if (cte_ctx_ && cte_ctx_->Has(query.from_table)) {
    const std::vector<RowMap>* cte_rows = cte_ctx_->Get(query.from_table);
    if (!cte_rows) return Status::InvalidArgument("missing cte: " + query.from_table);
    const Status budget = CheckRowBudget(cte_rows->size(), max_pages_);
    if (!budget.ok()) return budget;
    for (const auto& fields : *cte_rows) {
      RowMap eval_fields = fields;
      for (const auto& o : outer_row) {
        if (o.first.find('.') != std::string::npos) {
          eval_fields[o.first] = o.second;
        }
      }
      if (query.where && !EvalWhere(*query.where, eval_fields, level)) {
        if (!last_error_.ok()) return last_error_;
        continue;
      }
      out->push_back(fields);
      const Status out_budget = CheckRowBudget(out->size(), max_pages_);
      if (!out_budget.ok()) return out_budget;
    }
    return Status::Ok();
  }

  if (query.from_table.empty()) {
    RowMap row;
    row["key"] = "1";
    row["value"] = "1";
    if (!query.scalar_projects.empty()) {
      ExprEval eval;
      row["value"] = eval.EvalScalar(*query.scalar_projects[0], row);
      if (!query.project_cols.empty()) {
        row[query.project_cols[0]] = row["value"];
      }
    } else if (!query.project_cols.empty() && query.project_cols[0] != "*") {
      row["value"] = query.project_cols[0];
      row[query.project_cols[0]] = row["value"];
    }
    if (!query.where || EvalWhere(*query.where, row, level)) {
      out->push_back(row);
    }
    return Status::Ok();
  }

  return Status::InvalidArgument("unknown table: " + query.from_table);
}

}  // namespace sql
}  // namespace ebtree
