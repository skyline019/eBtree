#include "executor_v3.h"

#include <algorithm>
#include <functional>
#include <memory>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <unordered_set>

#include "sql/eval/agg_engine.h"
#include "sql/eval/type_affinity.h"
#include "sql/catalog/row_codec.h"
#include "sql/eval/expr_eval.h"
#include "sql/eval/schema_context.h"
#include "sql/exec/dml_executor.h"
#include "sql/exec/physical_scan.h"
#include "sql/exec/executor_v2.h"
#include "sql/exec/mic_guard.h"
#include "sql/exec/executor_v2.h"
#include "sql/exec/pragma_exec.h"
#include "sql/parse/core/stmt_classifier.h"
#include "sql/parse/native/native_parser.h"
#include "sql/plan/exec_plan.h"
#include "sql/plan/index_match.h"
#include "ebtree/common/digest.h"

namespace ebtree {
namespace sql {
namespace {

int64_t AsInt(const std::string& s) {
  try {
    return std::stoll(s);
  } catch (...) {
    return 0;
  }
}

std::string FieldVal(const RowMap& r, const std::string& col) {
  if (r.count(col)) return r.at(col);
  const auto dot = col.find('.');
  if (dot != std::string::npos) {
    const std::string qual = col;
    if (r.count(qual)) return r.at(qual);
    const std::string bare = col.substr(dot + 1);
    if (r.count(bare)) return r.at(bare);
  }
  if (r.count("value")) return r.at("value");
  if (r.count("key")) return r.at("key");
  return "";
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

void PopulateRowFields(const TableSchema& table, const std::string& user_key,
                       const std::string& raw_value,
                       std::unordered_map<std::string, std::string>* fields,
                       bool qualify = false) {
  fields->clear();
  if (table.schema_version >= 2) {
    (void)DecodeRowFields(raw_value, fields);
  }
  if (!table.implicit_rowid && !fields->count(table.key_column)) {
    (*fields)[table.key_column] = user_key;
  }
  if (table.schema_version < 2 &&
      (fields->empty() || !fields->count(table.value_column))) {
    fields->clear();
    if (!table.implicit_rowid) {
      (*fields)[table.key_column] = user_key;
    }
    (*fields)[table.value_column] = raw_value;
  }
  (*fields)["key"] = user_key;
  if (qualify) {
    const std::unordered_map<std::string, std::string> copy = *fields;
    for (const auto& kv : copy) {
      (*fields)[table.name + "." + kv.first] = kv.second;
    }
  }
}

}  // namespace

SqlExecutorV3::SqlExecutorV3(Engine* engine, Catalog* catalog,
                             CatalogStore* catalog_store, OpLogWriter* op_log,
                             DurabilityClass tier, TransactionState* txn)
    : engine_(engine),
      catalog_(catalog),
      catalog_store_(catalog_store),
      op_log_(op_log),
      tier_(tier),
      txn_(txn),
      base_(engine, catalog, op_log, tier, txn) {}

Status SqlExecutorV3::SaveCatalog() const {
  if (!catalog_store_) return Status::Ok();
  return catalog_store_->Save(*catalog_);
}

Status SqlExecutorV3::ScanTableRows(
    const TableSchema& table,
    std::vector<std::pair<std::string, std::string>>* rows) {
  PhysicalScan scan(engine_, catalog_, txn_);
  return scan.ScanTable(table, rows);
}

Status SqlExecutorV3::ExecSelectRich(const SelectQuery& sq,
                                     const TableSchema& table,
                                     ExecuteResult* out,
                                     const CteContext* cte_ctx,
                                     const ExprNode* extra_where) {
  if (sq.from_table.empty()) {
    RowMap row;
    row["key"] = "1";
    SubqueryRunner sub_runner(engine_, catalog_);
    sub_runner.SetMaxPages(sq.max_pages);
    if (cte_ctx) sub_runner.SetCteContext(cte_ctx);
    ExprEval eval;
    if (table.name.empty()) {
      SchemaContext schema_ctx;
      eval.SetSchemaContext(schema_ctx);
    } else {
      SchemaContext schema_ctx;
      schema_ctx.table = &table;
      eval.SetSchemaContext(schema_ctx);
    }
    eval.SetSubqueryEval([&sub_runner](const ExprNode& node, const RowMap& outer) {
      return sub_runner.EvalSubqueryTruth(node, outer, 0);
    });
    if (out) {
      for (const auto& sp : sq.scalar_projects) {
        SqlRow sr;
        sr.value = eval.EvalScalar(*sp, row);
        sr.key = sr.value;
        out->rows.push_back(sr);
      }
    }
    return Status::Ok();
  }
  const uint64_t pages_before = engine_->btree()->pages_touched();
  const bool from_cte = cte_ctx && cte_ctx->Has(sq.from_table);
  std::vector<std::pair<std::string, std::string>> encoded;
  if (!from_cte) {
    bool used_index = false;
    if (sq.joins.empty()) {
      plan::ExecPlan exec_plan{};
      const Status lp = plan::LowerSelectQuery(sq, catalog_, &exec_plan);
      if (lp.ok() && !exec_plan.steps.empty() &&
          exec_plan.steps[0].kind == plan::PlanStepKind::kIndexScan) {
        const plan::PlanStep& step = exec_plan.steps[0];
        const IndexDef* idx =
            plan::FindLeadingIndex(catalog_, table.name, step.index_column);
        if (idx) {
          PhysicalScan ps(engine_, catalog_, txn_);
          Status ss;
          if (step.index_scan_mode == plan::IndexScanMode::kEq) {
            ss = ps.ScanIndexEq(table, *idx, step.index_value, &encoded);
            if (!ss.ok()) return ss;
            used_index = true;
          } else if (step.index_scan_mode == plan::IndexScanMode::kRange) {
            ss = ps.ScanIndexRange(table, *idx, step.index_range_lo,
                                   step.index_range_hi, &encoded);
            if (!ss.ok()) return ss;
            used_index = true;
          }
        }
      }
    }
    if (!used_index) {
      const Status ss = ScanTableRows(table, &encoded);
      if (!ss.ok()) return ss;
    }
    const Status mic = CheckMicPagesBudget(engine_, pages_before, sq.max_pages);
    if (!mic.ok()) return mic;
    const Status rows_mic = CheckMicRowBudget(encoded.size(), sq.max_pages);
    if (!rows_mic.ok()) return rows_mic;
  }

  SubqueryRunner sub_runner(engine_, catalog_);
  sub_runner.SetMaxPages(sq.max_pages);
  if (cte_ctx) sub_runner.SetCteContext(cte_ctx);
  sub_runner.ClearLastError();
  if (sq.where) {
    const Status depth_st = SubqueryRunner::ValidateWhereDepth(*sq.where, 0);
    if (!depth_st.ok()) return depth_st;
  }
  ExprEval eval;
  SchemaContext schema_ctx;
  schema_ctx.table = &table;
  eval.SetSchemaContext(schema_ctx);
  sub_runner.SetSubqueryEval([&sub_runner](const ExprNode& node, const RowMap& outer) {
    return sub_runner.EvalSubqueryTruth(node, outer, 0);
  });
  eval.SetSubqueryEval([&sub_runner](const ExprNode& node, const RowMap& outer) {
    return sub_runner.EvalSubqueryTruth(node, outer, 0);
  });

  std::vector<std::unordered_map<std::string, std::string>> rows;
  if (from_cte) {
    const std::vector<RowMap>* cte_rows = cte_ctx->Get(sq.from_table);
    if (!cte_rows) {
      return Status::InvalidArgument("missing cte: " + sq.from_table);
    }
    for (const auto& base_row : *cte_rows) {
      if (extra_where && !eval.EvalBool(*extra_where, base_row)) {
        if (!sub_runner.LastError().ok()) return sub_runner.LastError();
        continue;
      }
      if (sq.where && !eval.EvalBool(*sq.where, base_row)) {
        if (!sub_runner.LastError().ok()) return sub_runner.LastError();
        continue;
      }
      rows.push_back(base_row);
    }
  } else {
    for (const auto& kv : encoded) {
      std::string user_key;
      if (!catalog_->DecodeRowKey(kv.first, nullptr, &user_key)) user_key = kv.first;
      std::unordered_map<std::string, std::string> fields;
      PopulateRowFields(table, user_key, kv.second, &fields, true);
      if (extra_where && !eval.EvalBool(*extra_where, fields)) {
        if (!sub_runner.LastError().ok()) return sub_runner.LastError();
        continue;
      }
      if (sq.where && !eval.EvalBool(*sq.where, fields)) {
        if (!sub_runner.LastError().ok()) return sub_runner.LastError();
        continue;
      }
      rows.push_back(fields);
    }
  }

  for (const auto& j : sq.joins) {
    const auto rt = catalog_->FindTable(j.table);
    if (!rt) return Status::InvalidArgument("unknown join table: " + j.table);
    std::vector<std::pair<std::string, std::string>> right_enc;
    const Status rs = ScanTableRows(*rt, &right_enc);
    if (!rs.ok()) return rs;
    std::vector<RowMap> right_rows;
    for (const auto& re : right_enc) {
      std::string rk;
      if (!catalog_->DecodeRowKey(re.first, nullptr, &rk)) rk = re.first;
      RowMap rf;
      PopulateRowFields(*rt, rk, re.second, &rf, true);
      right_rows.push_back(std::move(rf));
    }
    {
      const Status mic = CheckMicPagesBudget(engine_, pages_before, sq.max_pages);
      if (!mic.ok()) return mic;
      const Status rows_mic =
          CheckMicRowBudget(rows.size() + right_rows.size(), sq.max_pages);
      if (!rows_mic.ok()) return rows_mic;
    }
    std::vector<RowMap> joined;
    if (j.type == JoinType::kCross) {
      for (const auto& l : rows) {
        for (const auto& rf : right_rows) {
          auto merged = l;
          for (const auto& kv : rf) merged[kv.first] = kv.second;
          joined.push_back(std::move(merged));
        }
      }
    } else if (j.type == JoinType::kRight || j.type == JoinType::kFull) {
      for (const auto& rf : right_rows) {
        bool matched = false;
        const std::string rr = rf.count(j.right_col) ? rf.at(j.right_col)
                           : rf.count(j.table + "." + j.right_col)
                               ? rf.at(j.table + "." + j.right_col)
                               : "";
        for (const auto& l : rows) {
          const std::string lk = l.count(j.left_col) ? l.at(j.left_col)
                             : l.count(table.name + "." + j.left_col)
                                 ? l.at(table.name + "." + j.left_col)
                                 : "";
          if (!lk.empty() && lk == rr) {
            auto merged = l;
            for (const auto& kv : rf) merged[kv.first] = kv.second;
            joined.push_back(std::move(merged));
            matched = true;
          }
        }
        if ((j.type == JoinType::kRight || j.type == JoinType::kFull) && !matched) {
          joined.push_back(rf);
        }
      }
      if (j.type == JoinType::kFull) {
        for (const auto& l : rows) {
          bool matched = false;
          const std::string lk = l.count(j.left_col) ? l.at(j.left_col)
                             : l.count(table.name + "." + j.left_col)
                                 ? l.at(table.name + "." + j.left_col)
                                 : "";
          for (const auto& rf : right_rows) {
            const std::string rr = rf.count(j.right_col) ? rf.at(j.right_col)
                               : rf.count(j.table + "." + j.right_col)
                                   ? rf.at(j.table + "." + j.right_col)
                                   : "";
            if (!lk.empty() && lk == rr) {
              matched = true;
              break;
            }
          }
          if (!matched) joined.push_back(l);
        }
      }
    } else {
      for (const auto& l : rows) {
        bool matched = false;
        const std::string lk = l.count(j.left_col) ? l.at(j.left_col)
                           : l.count(table.name + "." + j.left_col)
                               ? l.at(table.name + "." + j.left_col)
                               : "";
        for (const auto& rf : right_rows) {
          const std::string rr = rf.count(j.right_col) ? rf.at(j.right_col)
                             : rf.count(j.table + "." + j.right_col)
                                 ? rf.at(j.table + "." + j.right_col)
                                 : "";
          if (!lk.empty() && lk == rr) {
            auto merged = l;
            for (const auto& kv : rf) merged[kv.first] = kv.second;
            joined.push_back(std::move(merged));
            matched = true;
          }
        }
        if (j.type == JoinType::kLeft && !matched) joined.push_back(l);
      }
    }
    rows = std::move(joined);
  }

  if (sq.distinct && sq.group_by.empty() && sq.aggregates.empty()) {
    std::vector<RowMap> unique_rows;
    std::unordered_set<std::string> seen;
    for (const auto& r : rows) {
      std::string key;
      if (!sq.project_cols.empty() && sq.project_cols[0] != "*") {
        for (size_t i = 0; i < sq.project_cols.size(); ++i) {
          if (i > 0) key.push_back('|');
          key += FieldVal(r, sq.project_cols[i]);
        }
      } else {
        key = FieldVal(r, table.key_column);
        const std::string val = FieldVal(r, table.value_column);
        if (!val.empty() && val != key) key += "|" + val;
      }
      if (seen.insert(key).second) unique_rows.push_back(r);
    }
    rows = std::move(unique_rows);
  }

  if (sq.group_by.empty() && !sq.aggregates.empty()) {
    for (const auto& agg : sq.aggregates) {
      const Status vs = AggEngine::Validate(agg);
      if (!vs.ok()) return vs;
    }
    std::unordered_map<std::string, AggBucket> per_agg;
    for (const auto& agg : sq.aggregates) {
      per_agg.emplace(agg.alias, AggBucket{});
    }
    for (const auto& r : rows) {
      for (const auto& agg : sq.aggregates) {
        AggEngine::Accumulate(&per_agg[agg.alias], agg, r);
      }
    }
    RowMap g;
    for (const auto& agg : sq.aggregates) {
      g[agg.alias] = AggEngine::Finalize(agg, per_agg[agg.alias], rows.size());
    }
    if (sq.having && !eval.EvalBool(*sq.having, g)) {
      rows.clear();
    } else {
      rows = {g};
    }
  } else if (!sq.group_by.empty()) {
    std::unordered_map<std::string, AggBucket> groups;
    std::unordered_map<std::string, RowMap> group_fields;
    std::unordered_map<std::string, size_t> group_counts;
    for (const auto& agg : sq.aggregates) {
      const Status vs = AggEngine::Validate(agg);
      if (!vs.ok()) return vs;
    }
    for (const auto& r : rows) {
      const std::string gk = FieldVal(r, sq.group_by[0]);
      AggBucket& b = groups[gk];
      group_fields[gk][sq.group_by[0]] = gk;
      group_counts[gk]++;
      for (const auto& agg : sq.aggregates) {
        AggEngine::Accumulate(&b, agg, r);
      }
    }
    rows.clear();
    for (auto& kv : groups) {
      RowMap g = group_fields[kv.first];
      for (const auto& agg : sq.aggregates) {
        if (agg.func == "COUNT" && !agg.distinct && agg.column == "*") {
          g[agg.alias] = std::to_string(group_counts[kv.first]);
        } else {
          g[agg.alias] =
              AggEngine::Finalize(agg, kv.second, group_counts[kv.first]);
        }
      }
      if (sq.having && !eval.EvalBool(*sq.having, g)) continue;
      rows.push_back(g);
    }
  }

  if (!sq.order_by.empty()) {
    const auto& col = sq.order_by[0].column;
    const bool desc = sq.order_by[0].descending;
    std::sort(rows.begin(), rows.end(),
              [&](const auto& a, const auto& b) {
                const std::string av = a.count(col) ? a.at(col) : "";
                const std::string bv = b.count(col) ? b.at(col) : "";
                return desc ? av > bv : av < bv;
              });
  }

  if (sq.limit.has_value() && rows.size() > *sq.limit) {
    rows.resize(static_cast<size_t>(*sq.limit));
  }

  if (out) {
    for (const auto& r : rows) {
      SqlRow row{};
      if (!sq.aggregates.empty()) {
        if (sq.aggregates.size() >= 2) {
          row.key = FieldVal(r, sq.aggregates[0].alias);
          row.value = FieldVal(r, sq.aggregates[1].alias);
        } else {
          row.value = FieldVal(r, sq.aggregates[0].alias);
          row.key = row.value;
        }
      } else if (!sq.scalar_projects.empty()) {
        row.value = eval.EvalScalar(*sq.scalar_projects[0], r);
        row.key = row.value;
      } else if (!sq.project_cols.empty() && sq.project_cols[0] != "*") {
        row.key = FieldVal(r, sq.project_cols[0]);
        if (sq.project_cols.size() >= 2) {
          row.value = FieldVal(r, sq.project_cols[1]);
        } else {
          row.value = row.key;
        }
      } else {
        row.key = FieldVal(r, sq.group_by.empty() ? table.key_column : sq.group_by[0]);
        if (r.count(table.value_column)) {
          row.value = r.at(table.value_column);
        } else {
          row.value = row.key;
        }
      }
      if (!sq.scalar_projects.empty() && sq.aggregates.empty()) {
        for (size_t i = 0; i < sq.scalar_projects.size(); ++i) {
          const ExprNode& sp = *sq.scalar_projects[i];
          if (sp.subquery.has_value() && sp.subquery->parsed_query) {
            std::vector<RowMap> sr;
            if (sub_runner.Run(*sp.subquery->parsed_query, r, &sr).ok() &&
                !sr.empty()) {
              const std::string proj = sp.subquery->parsed_query->project_cols.empty()
                                           ? "key"
                                           : sp.subquery->parsed_query->project_cols[0];
              row.value = FieldVal(sr[0], proj);
            }
          }
        }
      }
      out->rows.push_back(row);
    }
  }
  return CheckMicPagesBudget(engine_, pages_before, sq.max_pages);
}

static TableSchema VirtualTableSchema(const std::string& name) {
  TableSchema t{};
  t.name = name;
  t.key_column = "key";
  t.value_column = "value";
  t.schema_version = 1;
  return t;
}

static void RowsFromExecuteResult(const ExecuteResult& result,
                                  std::vector<RowMap>* out) {
  out->clear();
  for (const auto& sr : result.rows) {
    RowMap r;
    r["key"] = sr.key;
    r["value"] = sr.value;
    out->push_back(std::move(r));
  }
}

Status SqlExecutorV3::CollectSelectRows(const SelectQuery& sq,
                                        const CteContext* cte_ctx,
                                        std::vector<RowMap>* rows) {
  if (!rows) return Status::InvalidArgument("rows is null");
  std::string from_table = sq.from_table;
  if (auto view = catalog_->FindView(from_table)) {
    from_table = view->table;
  }
  const auto table = catalog_->FindTable(from_table);
  if (table) {
    ExecuteResult result{};
    const Status st = ExecSelectRich(sq, *table, &result, cte_ctx);
    if (!st.ok()) return st;
    RowsFromExecuteResult(result, rows);
    return Status::Ok();
  }
  if (cte_ctx && cte_ctx->Has(sq.from_table)) {
    const TableSchema virt = VirtualTableSchema(sq.from_table);
    ExecuteResult result{};
    const Status st = ExecSelectRich(sq, virt, &result, cte_ctx);
    if (!st.ok()) return st;
    RowsFromExecuteResult(result, rows);
    return Status::Ok();
  }
  return Status::InvalidArgument("unknown table: " + sq.from_table);
}

Status SqlExecutorV3::ExecCte(const QueryStatement& stmt, ExecuteResult* out) {
  if (!stmt.cte_query || !stmt.cte_query->main_query) {
    return Status::InvalidArgument("invalid CTE statement");
  }
  CteContext ctx;
  for (const auto& entry : stmt.cte_query->ctes) {
    if (!entry.query) {
      return Status::InvalidArgument("CTE missing query body");
    }
    std::vector<RowMap> materialized;
    const Status ms =
        CollectSelectRows(*entry.query, &ctx, &materialized);
    if (!ms.ok()) return ms;
    if (stmt.cte_query->recursive && !materialized.empty()) {
      for (int iter = 0; iter < 32; ++iter) {
        ctx.tables[entry.name] = materialized;
        std::vector<RowMap> extra;
        const Status rs = CollectSelectRows(*entry.query, &ctx, &extra);
        if (!rs.ok() || extra.empty()) break;
        const size_t before = materialized.size();
        for (const auto& r : extra) materialized.push_back(r);
        if (materialized.size() == before) break;
      }
    }
    ctx.tables[entry.name] = std::move(materialized);
  }
  const SelectQuery& mq = *stmt.cte_query->main_query;
  const auto table = catalog_->FindTable(mq.from_table);
  if (table) {
    return ExecSelectRich(mq, *table, out, &ctx);
  }
  if (ctx.Has(mq.from_table)) {
    const TableSchema virt = VirtualTableSchema(mq.from_table);
    return ExecSelectRich(mq, virt, out, &ctx);
  }
  return Status::InvalidArgument("unknown table: " + mq.from_table);
}

static std::string RowSignature(const RowMap& row,
                                const std::vector<std::string>& cols) {
  std::string sig;
  for (const auto& c : cols) {
    sig.push_back('|');
    sig += FieldVal(row, c);
  }
  return sig;
}

Status SqlExecutorV3::ExecSetOp(const QueryStatement& stmt, ExecuteResult* out) {
  if (!stmt.setop_query || !stmt.setop_query->left || !stmt.setop_query->right) {
    return Status::InvalidArgument("invalid set op statement");
  }
  const SetOpQuery& so = *stmt.setop_query;
  std::vector<RowMap> left_rows;
  std::vector<RowMap> right_rows;
  const Status ls = CollectSelectRows(*so.left, nullptr, &left_rows);
  if (!ls.ok()) return ls;
  const Status rs = CollectSelectRows(*so.right, nullptr, &right_rows);
  if (!rs.ok()) return rs;

  const auto& proj = so.left->project_cols.empty()
                         ? std::vector<std::string>{"key"}
                         : so.left->project_cols;

  std::vector<RowMap> merged;
  if (so.op == SetOpKind::kUnion) {
    if (so.all) {
      merged = left_rows;
      for (const auto& r : right_rows) merged.push_back(r);
    } else {
      std::unordered_map<std::string, RowMap> seen;
      for (const auto& r : left_rows) {
        seen[RowSignature(r, proj)] = r;
      }
      for (const auto& r : right_rows) {
        seen[RowSignature(r, proj)] = r;
      }
      for (const auto& kv : seen) merged.push_back(kv.second);
    }
  } else if (so.op == SetOpKind::kIntersect) {
    std::unordered_map<std::string, RowMap> right_map;
    for (const auto& r : right_rows) {
      right_map[RowSignature(r, proj)] = r;
    }
    for (const auto& r : left_rows) {
      if (right_map.count(RowSignature(r, proj))) {
        merged.push_back(r);
      }
    }
  } else {
    std::unordered_map<std::string, bool> right_keys;
    for (const auto& r : right_rows) {
      right_keys[RowSignature(r, proj)] = true;
    }
    for (const auto& r : left_rows) {
      if (!right_keys.count(RowSignature(r, proj))) {
        merged.push_back(r);
      }
    }
  }

  if (out) {
    for (const auto& r : merged) {
      SqlRow row{};
      row.key = FieldVal(r, proj[0]);
      row.value = proj.size() > 1 ? FieldVal(r, proj[1]) : row.key;
      out->rows.push_back(row);
    }
  }
  return Status::Ok();
}

Status SqlExecutorV3::ExecWindow(const QueryStatement& stmt, ExecuteResult* out) {
  if (!stmt.window_query || !stmt.window_query->query) {
    return Status::InvalidArgument("invalid window statement");
  }
  const WindowQuery& wq = *stmt.window_query;
  std::string func_upper = wq.window_func;
  for (char& c : func_upper) {
    c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
  }
  const bool is_row_number = func_upper.find("ROW_NUMBER") != std::string::npos;
  const bool is_rank = func_upper.find("RANK") != std::string::npos &&
                       func_upper.find("DENSE") == std::string::npos;
  const bool is_dense = func_upper.find("DENSE_RANK") != std::string::npos;
  if (!is_row_number && !is_rank && !is_dense) {
    return FeatureNotSupported("WINDOW function");
  }

  std::vector<RowMap> rows;
  const Status cs = CollectSelectRows(*wq.query, nullptr, &rows);
  if (!cs.ok()) return cs;

  const std::string partition = wq.partition_col;
  const std::string order_col = wq.order_col.empty() ? "key" : wq.order_col;
  std::sort(rows.begin(), rows.end(), [&](const RowMap& a, const RowMap& b) {
    const std::string pa = partition.empty() ? "" : FieldVal(a, partition);
    const std::string pb = partition.empty() ? "" : FieldVal(b, partition);
    if (pa != pb) return pa < pb;
    const std::string av = FieldVal(a, order_col);
    const std::string bv = FieldVal(b, order_col);
    return wq.order_desc ? av > bv : av < bv;
  });

  if (out) {
    std::string last_partition;
    int64_t row_num = 0;
    int64_t dense_rank = 0;
    int64_t sql_rank = 0;
    std::string last_order_val;
    for (const auto& r : rows) {
      const std::string pk = partition.empty() ? "" : FieldVal(r, partition);
      if (pk != last_partition) {
        row_num = 0;
        dense_rank = 0;
        sql_rank = 0;
        last_order_val.clear();
        last_partition = pk;
      }
      ++row_num;
      const std::string ov = FieldVal(r, order_col);
      if (ov != last_order_val) {
        ++dense_rank;
        sql_rank = row_num;
        last_order_val = ov;
      }
      SqlRow sr{};
      sr.key = FieldVal(r, "key");
      if (is_row_number) {
        sr.value = std::to_string(row_num);
      } else if (is_dense) {
        sr.value = std::to_string(dense_rank);
      } else {
        sr.value = std::to_string(sql_rank);
      }
      out->rows.push_back(sr);
    }
  }
  return Status::Ok();
}

Status SqlExecutorV3::ExecSelect(const QueryStatement& stmt, ExecuteResult* out) {
  if (stmt.select_rich && stmt.select_rich->from_table.empty()) {
    return ExecSelectRich(*stmt.select_rich, VirtualTableSchema(""), out);
  }
  const std::string from_name =
      stmt.select_rich ? stmt.select_rich->from_table : stmt.select.table;
  const std::optional<ViewDef> view = catalog_->FindView(from_name);
  const std::string table_name = view ? view->table : from_name;
  const auto table = catalog_->FindTable(table_name);
  if (!table) return Status::InvalidArgument("unknown table: " + from_name);
  const ExprNode* view_where = view ? view->where_filter.get() : nullptr;
  if (stmt.select_rich && stmt.select_rich->where) {
    const Status depth_st =
        SubqueryRunner::ValidateWhereDepth(*stmt.select_rich->where, 0);
    if (!depth_st.ok()) return depth_st;
  }
  if (view_where) {
    const Status depth_st = SubqueryRunner::ValidateWhereDepth(*view_where, 0);
    if (!depth_st.ok()) return depth_st;
  }
  if (stmt.select_rich) {
    return ExecSelectRich(*stmt.select_rich, *table, out, nullptr, view_where);
  }
  return Status::InvalidArgument("SELECT requires rich parse path");
}

Status SqlExecutorV3::ExecExplain(const QueryStatement& stmt,
                                  ExecuteResult* out) {
  std::string inner = stmt.raw_sql;
  std::string upper = inner;
  for (char& c : upper) {
    c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
  }
  const auto explain_pos = upper.find("EXPLAIN");
  if (explain_pos != std::string::npos) {
    inner = inner.substr(explain_pos + 7);
    while (!inner.empty() && inner.front() == ' ') inner.erase(inner.begin());
    std::string inner_upper = inner;
    for (char& c : inner_upper) {
      c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    }
    if (inner_upper.find("QUERY PLAN") == 0) {
      inner = inner.substr(10);
      while (!inner.empty() && inner.front() == ' ') inner.erase(inner.begin());
    }
  }
  parse::NativeParser parser;
  QueryStatement inner_stmt{};
  const Status ps = parser.Parse(inner, &inner_stmt);
  if (!ps.ok()) return ps;
  plan::ExecPlan plan{};
  const Status ls = plan::LowerQueryWithCatalog(inner_stmt, catalog_, &plan);
  if (!ls.ok()) return ls;
  std::string plan_text = "QUERY PLAN\n";
  for (const auto& step : plan.steps) {
    switch (step.kind) {
      case plan::PlanStepKind::kIndexScan:
        if (step.index_scan_mode == plan::IndexScanMode::kRange) {
          plan_text += "SEARCH TABLE " + step.table + " USING INDEX " +
                       step.index_column + " (>= " + step.index_range_lo +
                       " AND <= " + step.index_range_hi + ")\n";
        } else {
          plan_text += "SEARCH TABLE " + step.table + " USING INDEX " +
                       step.index_column + " (= " + step.index_value + ")\n";
        }
        break;
      case plan::PlanStepKind::kFilter:
        plan_text += "FILTER\n";
        break;
      case plan::PlanStepKind::kNestedLoopJoin:
        plan_text += "NESTED LOOP JOIN " + step.join_table + "\n";
        break;
      case plan::PlanStepKind::kHashAggregate:
        plan_text += "HASH AGGREGATE\n";
        break;
      case plan::PlanStepKind::kSortLimit:
        plan_text += "SORT LIMIT\n";
        break;
      default:
        plan_text += "SCAN TABLE " + step.table + "\n";
        break;
    }
  }
  if (out) {
    out->rows.push_back(SqlRow{"plan", plan_text});
  }
  return Status::Ok();
}

Status SqlExecutorV3::Execute(const QueryStatement& stmt, ExecuteResult* out) {
  if (out) out->rows.clear();
  switch (stmt.kind) {
    case QueryStmtKind::kWithCte:
      return ExecCte(stmt, out);
    case QueryStmtKind::kSetOp:
      return ExecSetOp(stmt, out);
    case QueryStmtKind::kWindowSelect:
      return ExecWindow(stmt, out);
    default:
      break;
  }

  if (IsParseOnlyKind(stmt.kind)) {
    return FeatureNotSupported(StmtClassName(parse::ClassifyStatement(stmt.raw_sql)));
  }

  if (stmt.kind == QueryStmtKind::kExplain) {
    return ExecExplain(stmt, out);
  }

  if (stmt.kind == QueryStmtKind::kPragma) {
    return ExecPragma(stmt, catalog_, out);
  }
  if (stmt.kind == QueryStmtKind::kCreateView) {
    const Status st = catalog_->CreateView(
        stmt.create_view.name, stmt.create_view.base_table,
        stmt.create_view.key_column, stmt.create_view.value_column,
        stmt.create_view.where_filter);
    if (!st.ok()) return st;
    return SaveCatalog();
  }
  if (stmt.kind == QueryStmtKind::kDropView) {
    const Status st = catalog_->DropView(stmt.drop_view);
    if (!st.ok()) return st;
    return SaveCatalog();
  }
  if (stmt.kind == QueryStmtKind::kCreateTrigger) {
    TriggerDef tr{};
    tr.name = stmt.create_trigger.name;
    tr.table = stmt.create_trigger.table;
    tr.event = stmt.create_trigger.event;
    tr.body_sql = stmt.create_trigger.body_sql;
    const Status st = catalog_->CreateTrigger(tr);
    if (!st.ok()) return st;
    return SaveCatalog();
  }
  if (stmt.kind == QueryStmtKind::kDropTrigger) {
    const Status st = catalog_->DropTrigger(stmt.drop_trigger);
    if (!st.ok()) return st;
    return SaveCatalog();
  }
  if (stmt.kind == QueryStmtKind::kReindex) {
    if (!stmt.reindex_target.empty() &&
        !catalog_->FindIndex(stmt.reindex_target) &&
        !catalog_->FindTable(stmt.reindex_target)) {
      return Status::InvalidArgument("no such index: " + stmt.reindex_target);
    }
    return Status::Ok();
  }

  plan::ExecPlan plan{};
  const Status ls = plan::LowerQuery(stmt, &plan);
  if (!ls.ok()) return ls;

  switch (stmt.kind) {
    case QueryStmtKind::kSelect:
      return ExecSelect(stmt, out);
    case QueryStmtKind::kAlterTable: {
      if (stmt.alter_table.action.find("ADD") != std::string::npos) {
        const auto sp = stmt.alter_table.action.rfind(' ');
        std::string col = sp == std::string::npos ? "c" : stmt.alter_table.action.substr(sp + 1);
        const Status st = catalog_->AddColumn(stmt.alter_table.table, {col, "TEXT"});
        if (!st.ok()) return st;
        return SaveCatalog();
      }
      return SaveCatalog();
    }
    case QueryStmtKind::kCreateIndex:
    case QueryStmtKind::kDropIndex:
    case QueryStmtKind::kUpsert: {
      SqlExecutorV2 v2(engine_, catalog_, catalog_store_, op_log_, tier_, txn_);
      return v2.Execute(stmt, out);
    }
    case QueryStmtKind::kUpdate: {
      DmlExecutor dml(engine_, catalog_, catalog_store_, op_log_, tier_, txn_);
      return dml.ExecUpdate(stmt);
    }
    default:
      break;
  }

  SqlExecutorV2 v2(engine_, catalog_, catalog_store_, op_log_, tier_, txn_);
  return v2.Execute(stmt, out);
}

}  // namespace sql
}  // namespace ebtree
