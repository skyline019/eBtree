#include "ebtree/concept/wal/wal_segment.h"

#include <fstream>

#include "ebtree/concept/memtable/memtable.h"

namespace ebtree {

bool WalSegmentReplayer::HasPending(WalWriter* wal, uint64_t after_lsn) {
  if (!wal) return false;
  return wal->max_lsn() > after_lsn;
}

Status WalSegmentReplayer::ReplayPending(WalWriter* wal, MemTable* mt,
                                         uint64_t after_lsn) {
  if (!wal || !mt) return Status::InvalidArgument("wal or memtable null");
  std::ifstream in(wal->path(), std::ios::binary);
  if (!in) return Status::Ok();
  while (in) {
    WalRecordHeader hdr{};
    in.read(reinterpret_cast<char*>(&hdr), sizeof(hdr));
    if (!in) break;
    std::string key(hdr.key_len, '\0');
    std::string value(hdr.value_len, '\0');
    if (hdr.key_len) in.read(key.data(), hdr.key_len);
    if (hdr.value_len) in.read(value.data(), hdr.value_len);
    if (hdr.lsn <= after_lsn) continue;
    if (hdr.op == WalOp::kDelete) {
      const Status st = mt->DeleteKey(key, hdr.lsn);
      if (!st.ok()) return st;
    } else {
      const Status st = mt->Put(key, value, hdr.lsn);
      if (!st.ok()) return st;
    }
  }
  return Status::Ok();
}

}  // namespace ebtree
