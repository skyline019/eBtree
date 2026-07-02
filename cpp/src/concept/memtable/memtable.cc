#include "ebtree/concept/memtable/memtable.h"

namespace ebtree {

Status MemTable::Put(const std::string& key, const std::string& value,
                     uint64_t lsn, uint32_t txn_id, bool durable) {
  std::lock_guard<std::mutex> lock(mu_);
  map_[key] = MemTableEntry{value, lsn, false, txn_id, durable};
  return Status::Ok();
}

Status MemTable::DeleteKey(const std::string& key, uint64_t lsn,
                           uint32_t txn_id, bool durable) {
  std::lock_guard<std::mutex> lock(mu_);
  map_[key] = MemTableEntry{"", lsn, true, txn_id, durable};
  return Status::Ok();
}

std::optional<MemTableEntry> MemTable::Get(const std::string& key) const {
  std::lock_guard<std::mutex> lock(mu_);
  const auto it = map_.find(key);
  if (it == map_.end()) return std::nullopt;
  return it->second;
}

std::vector<std::pair<std::string, MemTableEntry>> MemTable::Snapshot() const {
  std::lock_guard<std::mutex> lock(mu_);
  std::vector<std::pair<std::string, MemTableEntry>> out;
  out.reserve(map_.size());
  for (const auto& kv : map_) {
    out.emplace_back(kv.first, kv.second);
  }
  return out;
}

bool MemTable::Empty() const {
  std::lock_guard<std::mutex> lock(mu_);
  return map_.empty();
}

void MemTable::Clear() {
  std::lock_guard<std::mutex> lock(mu_);
  map_.clear();
}

void MemTable::Swap(MemTable* other) {
  if (!other) return;
  std::scoped_lock lock(mu_, other->mu_);
  map_.swap(other->map_);
}

void MemTable::PromoteTxn(uint32_t txn_id) {
  if (txn_id == 0) return;
  std::lock_guard<std::mutex> lock(mu_);
  for (auto& kv : map_) {
    if (kv.second.txn_id == txn_id) kv.second.durable = true;
  }
}

void MemTable::ClearTxn(uint32_t txn_id) {
  if (txn_id == 0) return;
  std::lock_guard<std::mutex> lock(mu_);
  for (auto it = map_.begin(); it != map_.end();) {
    if (it->second.txn_id == txn_id) {
      it = map_.erase(it);
    } else {
      ++it;
    }
  }
}

}  // namespace ebtree
