#include "ebtree/concept/wal/wal_segment.h"

#include <fstream>
#include <filesystem>
#include <unordered_set>
#include <vector>

#include "ebtree/common/crc32.h"
#include "ebtree/concept/memtable/memtable.h"

namespace ebtree {

namespace {

struct WalDataRecord {
  WalOp op{WalOp::kPut};
  std::string key;
  std::string value;
  uint64_t lsn{0};
  uint32_t txn_id{0};
};

uint64_t AlignUpWalSector(uint64_t offset) {
  return ((offset / kWalSectorSize) + 1) * kWalSectorSize;
}

bool DetectFormatV2OnDisk(const std::string& wal_path) {
  std::ifstream in(wal_path, std::ios::binary);
  if (!in) return false;
  if (in.seekg(0, std::ios::end).tellg() <
      static_cast<std::streamoff>(sizeof(kWalMagicV2))) {
    return false;
  }
  in.seekg(0);
  uint64_t magic = 0;
  in.read(reinterpret_cast<char*>(&magic), sizeof(magic));
  return static_cast<bool>(in) && magic == kWalMagicV2;
}

bool WalHeaderV2LooksValid(const WalRecordHeaderV2& hdr) {
  if (hdr.key_len > 65535 || hdr.value_len > 65535) return false;
  switch (hdr.op) {
    case WalOp::kPut:
    case WalOp::kDelete:
    case WalOp::kTxnBegin:
    case WalOp::kTxnCommit:
    case WalOp::kTxnAbort:
      return true;
    default:
      return false;
  }
}

bool WalHeaderV2IsPadding(const WalRecordHeaderV2& hdr) {
  return hdr.lsn == 0 && hdr.op == WalOp::kPut && hdr.key_len == 0 &&
         hdr.value_len == 0;
}

bool VerifyWalRecordV2Crc(const WalRecordHeaderV2& hdr) {
  WalRecordHeaderV2 copy = hdr;
  copy.record_crc = 0;
  return hdr.record_crc ==
         Crc32(&copy, sizeof(copy) - sizeof(copy.record_crc));
}

bool ReadWalRecordV2At(std::ifstream& in, uint64_t offset, uint64_t file_size,
                       WalRecordHeaderV2* hdr, std::string* key,
                       std::string* value, uint64_t* next_offset,
                       bool verify_crc) {
  if (offset + sizeof(WalRecordHeaderV2) > file_size) return false;
  in.clear();
  in.seekg(static_cast<std::streamoff>(offset));
  in.read(reinterpret_cast<char*>(hdr), sizeof(*hdr));
  if (!in) return false;
  if (WalHeaderV2IsPadding(*hdr) || !WalHeaderV2LooksValid(*hdr)) {
    return false;
  }
  const uint64_t record_end =
      offset + sizeof(WalRecordHeaderV2) + hdr->key_len + hdr->value_len;
  if (record_end > file_size) return false;
  key->assign(hdr->key_len, '\0');
  value->assign(hdr->value_len, '\0');
  if (hdr->key_len) in.read(key->data(), hdr->key_len);
  if (hdr->value_len) in.read(value->data(), hdr->value_len);
  if (!in) return false;
  if (verify_crc && !VerifyWalRecordV2Crc(*hdr)) return false;
  if (next_offset) *next_offset = record_end;
  return true;
}

bool HasValidWalRecordV2At(const std::string& wal_path, uint64_t offset) {
  std::error_code ec;
  const uint64_t file_size = std::filesystem::file_size(wal_path, ec);
  if (ec) return false;
  std::ifstream in(wal_path, std::ios::binary);
  if (!in) return false;
  WalRecordHeaderV2 hdr{};
  std::string key;
  std::string value;
  uint64_t next_offset = 0;
  return ReadWalRecordV2At(in, offset, file_size, &hdr, &key, &value,
                           &next_offset, true);
}

bool WalMagicAt(std::ifstream& in, uint64_t offset, uint64_t file_size) {
  if (offset + sizeof(kWalMagicV2) > file_size) return false;
  in.clear();
  in.seekg(static_cast<std::streamoff>(offset));
  uint64_t magic = 0;
  in.read(reinterpret_cast<char*>(&magic), sizeof(magic));
  return static_cast<bool>(in) && magic == kWalMagicV2;
}

uint64_t WalDataOffsetOnDisk(const std::string& wal_path, bool format_v2) {
  if (!format_v2) return 0;
  std::error_code ec;
  const auto sz = std::filesystem::file_size(wal_path, ec);
  if (ec || sz <= sizeof(kWalMagicV2)) return sizeof(kWalMagicV2);
  if (HasValidWalRecordV2At(wal_path, sizeof(kWalMagicV2))) {
    return sizeof(kWalMagicV2);
  }
  const uint64_t after_embedded_magic = sizeof(kWalMagicV2) * 2;
  if (sz > after_embedded_magic &&
      HasValidWalRecordV2At(wal_path, after_embedded_magic)) {
    return after_embedded_magic;
  }
  for (uint64_t sector = kWalSectorSize; sector < sz;
       sector += kWalSectorSize) {
    if (HasValidWalRecordV2At(wal_path, sector)) return sector;
    const uint64_t after_magic = sector + sizeof(kWalMagicV2);
    if (after_magic < sz && HasValidWalRecordV2At(wal_path, after_magic)) {
      return after_magic;
    }
  }
  return sizeof(kWalMagicV2);
}

bool TryReadWalRecordV2Sparse(std::ifstream& in, uint64_t offset,
                              uint64_t file_size, WalRecordHeaderV2* hdr,
                              std::string* key, std::string* value,
                              uint64_t* next_offset) {
  if (offset >= sizeof(kWalMagicV2) && WalMagicAt(in, offset, file_size)) {
    const uint64_t after_magic = offset + sizeof(kWalMagicV2);
    if (ReadWalRecordV2At(in, after_magic, file_size, hdr, key, value,
                         next_offset, true)) {
      return true;
    }
  }
  if (ReadWalRecordV2At(in, offset, file_size, hdr, key, value, next_offset,
                        true)) {
    return true;
  }
  if (offset >= kWalSectorSize && WalMagicAt(in, offset, file_size)) {
    const uint64_t after_magic = offset + sizeof(kWalMagicV2);
    return ReadWalRecordV2At(in, after_magic, file_size, hdr, key, value,
                             next_offset, true);
  }
  return false;
}

template <typename RecordFn>
void ScanWalV2Sparse(const std::string& wal_path, uint64_t after_lsn,
                     RecordFn&& on_record) {
  std::ifstream in(wal_path, std::ios::binary);
  if (!in) return;
  std::error_code ec;
  const uint64_t file_size =
      std::filesystem::file_size(wal_path, ec);
  if (ec) return;
  uint64_t offset = WalDataOffsetOnDisk(wal_path, true);
  while (offset < file_size) {
    WalRecordHeaderV2 hdr{};
    std::string key;
    std::string value;
    uint64_t next_offset = 0;
    if (!TryReadWalRecordV2Sparse(in, offset, file_size, &hdr, &key, &value,
                                  &next_offset)) {
      offset = AlignUpWalSector(offset);
      continue;
    }
    if (hdr.lsn > after_lsn) {
      on_record(hdr, key, value);
    }
    offset = next_offset;
  }
}

uint64_t ScanMaxLsnV2Linear(const std::string& wal_path, uint64_t after_lsn) {
  std::ifstream in(wal_path, std::ios::binary);
  if (!in) return 0;
  std::error_code ec;
  const uint64_t file_size = std::filesystem::file_size(wal_path, ec);
  if (ec) return 0;
  uint64_t offset = WalDataOffsetOnDisk(wal_path, true);
  uint64_t max_lsn = 0;
  while (offset < file_size) {
    WalRecordHeaderV2 hdr{};
    std::string key;
    std::string value;
    uint64_t next_offset = 0;
    if (!TryReadWalRecordV2Sparse(in, offset, file_size, &hdr, &key, &value,
                                  &next_offset)) {
      offset = AlignUpWalSector(offset);
      continue;
    }
    if (hdr.lsn > after_lsn && hdr.lsn > max_lsn) max_lsn = hdr.lsn;
    offset = next_offset;
  }
  return max_lsn;
}

uint64_t ScanMaxLsnV1AfterMagic(const std::string& wal_path, uint64_t after_lsn) {
  std::ifstream in(wal_path, std::ios::binary);
  if (!in) return 0;
  std::error_code ec;
  const uint64_t file_size = std::filesystem::file_size(wal_path, ec);
  if (ec || file_size <= sizeof(kWalMagicV2)) return 0;
  uint64_t offset = sizeof(kWalMagicV2);
  uint64_t max_lsn = 0;
  while (offset + sizeof(WalRecordHeader) <= file_size) {
    in.clear();
    in.seekg(static_cast<std::streamoff>(offset));
    WalRecordHeader hdr{};
    in.read(reinterpret_cast<char*>(&hdr), sizeof(hdr));
    if (!in) break;
    if (hdr.key_len > 65535 || hdr.value_len > 65535) break;
    const uint64_t record_end =
        offset + sizeof(WalRecordHeader) + hdr.key_len + hdr.value_len;
    if (record_end > file_size) break;
    in.seekg(static_cast<std::streamoff>(offset + sizeof(WalRecordHeader)));
    in.ignore(hdr.key_len + hdr.value_len);
    if (!in) break;
    if (hdr.lsn > after_lsn && hdr.lsn > max_lsn) max_lsn = hdr.lsn;
    offset = record_end;
  }
  return max_lsn;
}

uint64_t ScanMaxLsnV1(std::ifstream& in, uint64_t after_lsn) {
  uint64_t max_lsn = 0;
  in.clear();
  in.seekg(0);
  while (in) {
    WalRecordHeader hdr{};
    in.read(reinterpret_cast<char*>(&hdr), sizeof(hdr));
    if (!in) break;
    std::string key(hdr.key_len, '\0');
    std::string value(hdr.value_len, '\0');
    if (hdr.key_len) in.read(key.data(), hdr.key_len);
    if (hdr.value_len) in.read(value.data(), hdr.value_len);
    if (!in) break;
    if (hdr.lsn <= after_lsn) continue;
    if (hdr.lsn > max_lsn) max_lsn = hdr.lsn;
  }
  return max_lsn;
}

Status ScanWalV2Records(const std::string& wal_path, uint64_t after_lsn,
                        std::unordered_set<uint32_t>* committed_txns,
                        std::unordered_set<uint32_t>* aborted_txns,
                        std::vector<WalDataRecord>* data_records) {
  ScanWalV2Sparse(wal_path, after_lsn,
                  [&](const WalRecordHeaderV2& hdr, const std::string& key,
                      const std::string& value) {
                    if (hdr.op == WalOp::kTxnCommit) {
                      committed_txns->insert(hdr.txn_id);
                      return;
                    }
                    if (hdr.op == WalOp::kTxnAbort) {
                      aborted_txns->insert(hdr.txn_id);
                      return;
                    }
                    if (hdr.op == WalOp::kTxnBegin) return;
                    if (hdr.op != WalOp::kPut && hdr.op != WalOp::kDelete) {
                      return;
                    }
                    data_records->push_back(
                        WalDataRecord{hdr.op, key, value, hdr.lsn, hdr.txn_id});
                  });
  return Status::Ok();
}

void ScanWalV2TailExtentFrom(const std::string& wal_path, uint64_t start_offset,
                             uint64_t* data_end_out, uint64_t* max_lsn_out,
                             WalSegmentReplayer::TailRecordFn on_record) {
  std::ifstream in(wal_path, std::ios::binary);
  if (!in) return;
  std::error_code ec;
  const uint64_t file_size = std::filesystem::file_size(wal_path, ec);
  if (ec) return;
  uint64_t offset = std::max(start_offset, sizeof(kWalMagicV2));
  uint64_t data_end = start_offset;
  uint64_t max_lsn = 0;
  while (offset < file_size) {
    WalRecordHeaderV2 hdr{};
    std::string key;
    std::string value;
    uint64_t next_offset = 0;
    if (!TryReadWalRecordV2Sparse(in, offset, file_size, &hdr, &key, &value,
                                  &next_offset)) {
      offset = AlignUpWalSector(offset);
      continue;
    }
    data_end = next_offset;
    if (hdr.lsn > max_lsn) max_lsn = hdr.lsn;
    if (on_record) on_record(offset, hdr, key);
    offset = next_offset;
  }
  if (data_end_out) *data_end_out = data_end;
  if (max_lsn_out) *max_lsn_out = max_lsn;
}

void ScanWalV2TailExtent(const std::string& wal_path, uint64_t* data_end_out,
                           uint64_t* max_lsn_out) {
  std::ifstream in(wal_path, std::ios::binary);
  if (!in) return;
  std::error_code ec;
  const uint64_t file_size = std::filesystem::file_size(wal_path, ec);
  if (ec) return;
  uint64_t offset = WalDataOffsetOnDisk(wal_path, true);
  uint64_t data_end = offset;
  uint64_t max_lsn = 0;
  while (offset < file_size) {
    WalRecordHeaderV2 hdr{};
    std::string key;
    std::string value;
    uint64_t next_offset = 0;
    if (!TryReadWalRecordV2Sparse(in, offset, file_size, &hdr, &key, &value,
                                  &next_offset)) {
      offset = AlignUpWalSector(offset);
      continue;
    }
    data_end = next_offset;
    if (hdr.lsn > max_lsn) max_lsn = hdr.lsn;
    offset = next_offset;
  }
  if (data_end_out) *data_end_out = data_end;
  if (max_lsn_out) *max_lsn_out = max_lsn;
}

}  // namespace

uint64_t WalSegmentReplayer::DataOffsetOnDisk(const std::string& wal_path) {
  return WalDataOffsetOnDisk(wal_path, DetectFormatV2OnDisk(wal_path));
}

uint64_t WalSegmentReplayer::MaxLsnOnDisk(const std::string& wal_path,
                                          uint64_t after_lsn) {
  const bool format_v2 = DetectFormatV2OnDisk(wal_path);
  if (format_v2) {
    uint64_t max_lsn = 0;
    ScanWalV2Sparse(wal_path, after_lsn,
                    [&](const WalRecordHeaderV2& hdr, const std::string&,
                        const std::string&) {
                      if (hdr.lsn > max_lsn) max_lsn = hdr.lsn;
                    });
    if (max_lsn <= after_lsn) {
      max_lsn = std::max(max_lsn, ScanMaxLsnV2Linear(wal_path, after_lsn));
      max_lsn = std::max(max_lsn, ScanMaxLsnV1AfterMagic(wal_path, after_lsn));
    }
    return max_lsn;
  }
  std::ifstream in(wal_path, std::ios::binary);
  if (!in) return 0;
  return ScanMaxLsnV1(in, after_lsn);
}

bool WalSegmentReplayer::HasPendingOnDisk(const std::string& wal_path,
                                          uint64_t after_lsn) {
  return MaxLsnOnDisk(wal_path, after_lsn) > after_lsn;
}

bool WalSegmentReplayer::HasPending(WalWriter* wal, uint64_t after_lsn) {
  if (!wal) return false;
  if (wal->max_lsn() > after_lsn) return true;
  return HasPendingOnDisk(wal->path(), after_lsn);
}

uint64_t WalSegmentReplayer::LatestCommittedLsnForKey(const std::string& wal_path,
                                                     const std::string& key,
                                                     uint64_t after_lsn) {
  if (!DetectFormatV2OnDisk(wal_path)) {
    std::ifstream in(wal_path, std::ios::binary);
    if (!in) return 0;
    uint64_t best = 0;
    in.seekg(0);
    while (in) {
      WalRecordHeader hdr{};
      in.read(reinterpret_cast<char*>(&hdr), sizeof(hdr));
      if (!in) break;
      std::string wal_key(hdr.key_len, '\0');
      std::string wal_value(hdr.value_len, '\0');
      if (hdr.key_len) in.read(wal_key.data(), hdr.key_len);
      if (hdr.value_len) in.read(wal_value.data(), hdr.value_len);
      if (!in) break;
      if (hdr.lsn <= after_lsn) continue;
      if (wal_key != key) continue;
      if (hdr.lsn >= best) best = hdr.lsn;
    }
    return best;
  }

  std::unordered_set<uint32_t> committed_txns;
  std::unordered_set<uint32_t> aborted_txns;
  std::vector<WalDataRecord> data_records;
  if (!ScanWalV2Records(wal_path, after_lsn, &committed_txns, &aborted_txns,
                        &data_records)
           .ok()) {
    return 0;
  }

  uint64_t best = 0;
  for (const WalDataRecord& rec : data_records) {
    if (rec.key != key) continue;
    if (aborted_txns.count(rec.txn_id) > 0) continue;
    if (rec.txn_id != 0 && committed_txns.count(rec.txn_id) == 0) continue;
    if (rec.lsn >= best) best = rec.lsn;
  }
  return best;
}

Status WalSegmentReplayer::ExtendTailFromOffset(const std::string& wal_path,
                                                uint64_t start_offset,
                                                uint64_t* data_end_out,
                                                uint64_t* max_lsn_out,
                                                TailRecordFn on_record) {
  if (!data_end_out || !max_lsn_out) {
    return Status::InvalidArgument("null out");
  }
  if (!std::filesystem::exists(wal_path)) return Status::Ok();
  if (DetectFormatV2OnDisk(wal_path)) {
    ScanWalV2TailExtentFrom(wal_path, start_offset, data_end_out, max_lsn_out,
                            std::move(on_record));
    return Status::Ok();
  }
  std::ifstream in(wal_path, std::ios::binary);
  if (!in) return Status::Ok();
  uint64_t data_end = start_offset;
  uint64_t max_lsn = 0;
  in.seekg(static_cast<std::streamoff>(start_offset));
  while (in) {
    const uint64_t offset = static_cast<uint64_t>(in.tellg());
    WalRecordHeader hdr{};
    in.read(reinterpret_cast<char*>(&hdr), sizeof(hdr));
    if (!in) break;
    std::string key(hdr.key_len, '\0');
    std::string value(hdr.value_len, '\0');
    if (hdr.key_len) in.read(key.data(), hdr.key_len);
    if (hdr.value_len) in.read(value.data(), hdr.value_len);
    if (!in) break;
    data_end = static_cast<uint64_t>(in.tellg());
    if (hdr.lsn > max_lsn) max_lsn = hdr.lsn;
    if (on_record) {
      WalRecordHeaderV2 mapped{};
      mapped.lsn = hdr.lsn;
      mapped.op = hdr.op;
      mapped.key_len = hdr.key_len;
      mapped.value_len = hdr.value_len;
      on_record(offset, mapped, key);
    }
  }
  *data_end_out = data_end;
  *max_lsn_out = max_lsn;
  return Status::Ok();
}

Status WalSegmentReplayer::TailExtentOnDisk(const std::string& wal_path,
                                            uint64_t* data_end_out,
                                            uint64_t* max_lsn_out) {
  if (!data_end_out || !max_lsn_out) {
    return Status::InvalidArgument("null out");
  }
  *data_end_out = 0;
  *max_lsn_out = 0;
  if (!std::filesystem::exists(wal_path)) return Status::Ok();
  if (DetectFormatV2OnDisk(wal_path)) {
    ScanWalV2TailExtent(wal_path, data_end_out, max_lsn_out);
    return Status::Ok();
  }
  std::ifstream in(wal_path, std::ios::binary);
  if (!in) return Status::Ok();
  uint64_t data_end = 0;
  uint64_t max_lsn = 0;
  while (in) {
    const uint64_t offset = static_cast<uint64_t>(in.tellg());
    WalRecordHeader hdr{};
    in.read(reinterpret_cast<char*>(&hdr), sizeof(hdr));
    if (!in) break;
    std::string key(hdr.key_len, '\0');
    std::string value(hdr.value_len, '\0');
    if (hdr.key_len) in.read(key.data(), hdr.key_len);
    if (hdr.value_len) in.read(value.data(), hdr.value_len);
    if (!in) break;
    data_end = static_cast<uint64_t>(in.tellg());
    if (hdr.lsn > max_lsn) max_lsn = hdr.lsn;
    (void)offset;
  }
  *data_end_out = data_end;
  *max_lsn_out = max_lsn;
  return Status::Ok();
}

Status WalSegmentReplayer::ReplayPending(WalWriter* wal, MemTable* mt,
                                         uint64_t after_lsn) {
  if (!wal || !mt) return Status::InvalidArgument("wal or memtable null");
  const bool format_v2 = DetectFormatV2OnDisk(wal->path());
  if (!format_v2) {
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
      if (!in) break;
      if (hdr.lsn <= after_lsn) continue;
      if (hdr.op == WalOp::kDelete) {
        const Status st = mt->DeleteKey(key, hdr.lsn, 0, false);
        if (!st.ok()) return st;
      } else {
        const Status st = mt->Put(key, value, hdr.lsn, 0, false);
        if (!st.ok()) return st;
      }
    }
    return Status::Ok();
  }

  std::unordered_set<uint32_t> committed_txns;
  std::unordered_set<uint32_t> aborted_txns;
  std::vector<WalDataRecord> data_records;
  const Status st = ScanWalV2Records(wal->path(), after_lsn, &committed_txns,
                                     &aborted_txns, &data_records);
  if (!st.ok()) return st;

  for (const WalDataRecord& rec : data_records) {
    if (aborted_txns.count(rec.txn_id) > 0) continue;
    const bool durable =
        rec.txn_id == 0 || committed_txns.count(rec.txn_id) > 0;
    if (rec.op == WalOp::kDelete) {
      const Status del_st = mt->DeleteKey(rec.key, rec.lsn, rec.txn_id, durable);
      if (!del_st.ok()) return del_st;
    } else {
      const Status put_st =
          mt->Put(rec.key, rec.value, rec.lsn, rec.txn_id, durable);
      if (!put_st.ok()) return put_st;
    }
  }
  return Status::Ok();
}

}  // namespace ebtree
