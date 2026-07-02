#include "ebtree/concept/wal/wal_key_index.h"

#include <fstream>

#include "ebtree/concept/wal/wal.h"

namespace ebtree {

void WalKeyIndex::Clear() { index_.clear(); }

void WalKeyIndex::Update(uint64_t offset, const std::string& key, uint64_t lsn) {
  index_[key] = Entry{offset, lsn};
}

bool WalKeyIndex::Lookup(const std::string& key, uint64_t after_lsn,
                         uint64_t* offset_out) const {
  const auto it = index_.find(key);
  if (it == index_.end()) return false;
  if (it->second.lsn <= after_lsn) return false;
  if (offset_out) *offset_out = it->second.offset;
  return true;
}

bool WalKeyIndex::LatestLsn(const std::string& key, uint64_t after_lsn,
                            uint64_t* lsn_out) const {
  const auto it = index_.find(key);
  if (it == index_.end()) return false;
  if (it->second.lsn <= after_lsn) return false;
  if (lsn_out) *lsn_out = it->second.lsn;
  return true;
}

Status WalKeyIndex::BuildFromFile(const std::string& path) {
  Clear();
  std::ifstream in(path, std::ios::binary);
  if (!in) return Status::Ok();
  while (in) {
    const auto offset = static_cast<uint64_t>(in.tellg());
    WalRecordHeader hdr{};
    in.read(reinterpret_cast<char*>(&hdr), sizeof(hdr));
    if (!in) break;
    std::string key(hdr.key_len, '\0');
    std::string value(hdr.value_len, '\0');
    if (hdr.key_len) in.read(key.data(), hdr.key_len);
    if (hdr.value_len) in.read(value.data(), hdr.value_len);
    Update(offset, key, hdr.lsn);
  }
  return Status::Ok();
}

}  // namespace ebtree
