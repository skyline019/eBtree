#include "ebtree/concept/datafile/datafile_lsn_index.h"

#include <fstream>

#include "ebtree/common/crc32.h"
#include "ebtree/concept/datafile/datafile.h"

namespace ebtree {

namespace {

constexpr uint64_t kDidxMagic = 0xEB444958414445ULL;

}  // namespace

void DataFileLsnIndex::Clear() { index_.clear(); }

void DataFileLsnIndex::Update(uint64_t offset, uint64_t lsn) {
  index_[lsn] = offset;
}

bool DataFileLsnIndex::Lookup(uint64_t lsn, uint64_t* offset_out) const {
  const auto it = index_.find(lsn);
  if (it == index_.end()) return false;
  if (offset_out) *offset_out = it->second;
  return true;
}

Status DataFileLsnIndex::BuildFromFile(const std::string& path) {
  Clear();
  std::ifstream in(path, std::ios::binary);
  if (!in) return Status::Ok();
  while (in) {
    const auto offset = static_cast<uint64_t>(in.tellg());
    DataRecordHeader hdr{};
    in.read(reinterpret_cast<char*>(&hdr), sizeof(hdr));
    if (!in) break;
    std::string key(hdr.key_len, '\0');
    std::string value(hdr.value_len, '\0');
    if (hdr.key_len) in.read(key.data(), hdr.key_len);
    if (hdr.value_len) in.read(value.data(), hdr.value_len);
    Update(offset, hdr.lsn);
  }
  return Status::Ok();
}

Status DataFileLsnIndex::SaveToFile(const std::string& path) const {
  std::ofstream out(path, std::ios::binary | std::ios::trunc);
  if (!out) return Status::IoError("didx create failed");
  const uint64_t magic = kDidxMagic;
  const uint64_t count = index_.size();
  out.write(reinterpret_cast<const char*>(&magic), sizeof(magic));
  out.write(reinterpret_cast<const char*>(&count), sizeof(count));
  for (const auto& kv : index_) {
    const uint64_t lsn = kv.first;
    const uint64_t offset = kv.second;
    out.write(reinterpret_cast<const char*>(&lsn), sizeof(lsn));
    out.write(reinterpret_cast<const char*>(&offset), sizeof(offset));
  }
  if (!out) return Status::IoError("didx write failed");
  return Status::Ok();
}

Status DataFileLsnIndex::LoadFromFile(const std::string& path) {
  Clear();
  std::ifstream in(path, std::ios::binary);
  if (!in) return Status::NotFound("didx missing");
  uint64_t magic = 0;
  uint64_t count = 0;
  in.read(reinterpret_cast<char*>(&magic), sizeof(magic));
  if (!in) return Status::CorruptPage("truncated didx");
  if (magic != kDidxMagic) return Status::CorruptPage("bad didx magic");
  in.read(reinterpret_cast<char*>(&count), sizeof(count));
  if (!in) return Status::CorruptPage("truncated didx header");
  for (uint64_t i = 0; i < count; ++i) {
    uint64_t lsn = 0;
    uint64_t offset = 0;
    in.read(reinterpret_cast<char*>(&lsn), sizeof(lsn));
    in.read(reinterpret_cast<char*>(&offset), sizeof(offset));
    if (!in) return Status::CorruptPage("truncated didx entry");
    Update(offset, lsn);
  }
  return Status::Ok();
}

}  // namespace ebtree
