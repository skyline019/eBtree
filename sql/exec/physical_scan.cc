#include "physical_scan.h"

#include "ebtree/engine/engine.h"

namespace ebtree {
namespace sql {

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
  const Status ss = engine_->Scan(plan, &raw);
  if (!ss.ok()) return ss;
  rows->clear();
  for (const auto& kv : raw) {
    if (Catalog::IsIndexEncodedKey(kv.first)) continue;
    rows->push_back(kv);
  }
  return Status::Ok();
}

}  // namespace sql
}  // namespace ebtree
