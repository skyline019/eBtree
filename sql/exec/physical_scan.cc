#include "physical_scan.h"

#include <unordered_set>

#include "sql/catalog/row_codec.h"
#include "sql/exec/txn_read_policy.h"
#include "ebtree/engine/engine.h"

namespace ebtree {
namespace sql {

Status PhysicalScan::EngineGet(const std::string& key, std::string* value) const {
  return TxnGet(engine_, txn_, key, value);
}

Status PhysicalScan::EngineScan(
    const TypedPlan& plan,
    std::vector<std::pair<std::string, std::string>>* rows) const {
  const Status st = TxnScan(engine_, txn_, plan, rows);
  if (!st.ok()) return st;
  if (txn_ && txn_->active() && rows) {
    for (const auto& kv : *rows) {
      txn_->RecordReadSample(engine_, kv.first);
    }
  }
  if (txn_ && txn_->active() && plan.op == PredicateOp::kRange) {
    txn_->RegisterRangeScan(engine_, plan.key, plan.range_end);
  }
  return st;
}

Status PhysicalScan::FetchRowsFromIndexKeys(
    const TableSchema& table,
    const std::vector<std::pair<std::string, std::string>>& index_rows,
    std::vector<std::pair<std::string, std::string>>* rows) const {
  if (!rows) return Status::InvalidArgument("rows is null");
  rows->clear();
  std::unordered_set<std::string> seen;
  for (const auto& kv : index_rows) {
    std::string pk;
    if (!catalog_->DecodeIndexKeyPrefix(kv.first, nullptr, nullptr, nullptr,
                                       &pk)) {
      pk = kv.second;
    }
    const std::string encoded = catalog_->EncodeRowKey(table.id, pk);
    if (!seen.insert(encoded).second) continue;
    std::string raw;
    const Status gs = EngineGet(encoded, &raw);
    if (!gs.ok()) continue;
    rows->push_back({encoded, raw});
  }
  return Status::Ok();
}

Status PhysicalScan::ScanTable(
    const TableSchema& table,
    std::vector<std::pair<std::string, std::string>>* rows) const {
  if (!rows) return Status::InvalidArgument("rows is null");
  TypedPlan plan{};
  const std::string prefix = std::to_string(table.id) + ":";
  plan.op = PredicateOp::kRange;
  plan.key = prefix;
  plan.range_end = std::to_string(table.id + 1) + ":";
  const Status ps = engine_->Prepare(plan);
  if (!ps.ok()) return ps;
  std::vector<std::pair<std::string, std::string>> raw;
  const Status ss = EngineScan(plan, &raw);
  if (!ss.ok()) return ss;
  rows->clear();
  for (const auto& kv : raw) {
    if (Catalog::IsIndexEncodedKey(kv.first)) continue;
    rows->push_back(kv);
  }
  return Status::Ok();
}

Status PhysicalScan::ScanIndexEq(
    const TableSchema& table, const IndexDef& idx, const std::string& col_value,
    std::vector<std::pair<std::string, std::string>>* rows) const {
  if (!rows) return Status::InvalidArgument("rows is null");
  const std::string prefix = std::to_string(table.id) + ":i" +
                             std::to_string(idx.id) + ":" + col_value + ":";
  TypedPlan plan{};
  plan.op = PredicateOp::kRange;
  plan.key = prefix;
  plan.range_end = prefix + "\xFF";
  const Status ps = engine_->Prepare(plan);
  if (!ps.ok()) return ps;
  std::vector<std::pair<std::string, std::string>> index_rows;
  const Status ss = EngineScan(plan, &index_rows);
  if (!ss.ok()) return ss;
  return FetchRowsFromIndexKeys(table, index_rows, rows);
}

Status PhysicalScan::ScanIndexRange(
    const TableSchema& table, const IndexDef& idx, const std::string& range_lo,
    const std::string& range_hi,
    std::vector<std::pair<std::string, std::string>>* rows) const {
  if (!rows) return Status::InvalidArgument("rows is null");
  if (range_lo > range_hi) {
    rows->clear();
    return Status::Ok();
  }
  const std::string prefix_base =
      std::to_string(table.id) + ":i" + std::to_string(idx.id) + ":";
  TypedPlan plan{};
  plan.op = PredicateOp::kRange;
  plan.key = prefix_base + range_lo + ":";
  plan.range_end = prefix_base + range_hi + ":\xFF";
  const Status ps = engine_->Prepare(plan);
  if (!ps.ok()) return ps;
  std::vector<std::pair<std::string, std::string>> index_rows;
  const Status ss = EngineScan(plan, &index_rows);
  if (!ss.ok()) return ss;
  return FetchRowsFromIndexKeys(table, index_rows, rows);
}

}  // namespace sql
}  // namespace ebtree
