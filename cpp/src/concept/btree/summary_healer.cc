#include "ebtree/concept/btree/summary_healer.h"

namespace ebtree {

Status SummaryHealer::RebuildFromIndex(BTreeIndex* btree) {
  if (!btree) return Status::InvalidArgument("btree is null");
  btree->RebuildSummaryFromIndex();
  return Status::Ok();
}

Status SummaryHealer::RebuildFromCommitted(
    BTreeIndex* btree,
    const std::unordered_map<std::string, std::pair<std::string, uint64_t>>&
        committed) {
  if (!btree) return Status::InvalidArgument("btree is null");
  std::map<std::string, uint64_t> btree_map;
  for (const auto& kv : committed) {
    btree_map[kv.first] = kv.second.second;
  }
  if (btree->on_disk_mode()) {
    btree->RebuildSummaryFromCommitted(btree_map);
    return Status::Ok();
  }
  btree->LoadFromMap(btree_map);
  return Status::Ok();
}

}  // namespace ebtree
