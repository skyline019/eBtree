#include "ebtree/concept/datafile/datafile.h"

#include <filesystem>
#include <cstddef>
#include <cstring>
#include <algorithm>
#include <unordered_map>
#include <vector>

#include "ebtree/common/crc32.h"
#include "ebtree/concept/codec/value_codec.h"
#include "ebtree/concept/mmap/mmap_window.h"

namespace ebtree {

namespace {

uint8_t RecordCodec(const DataRecordHeader& hdr) {
  if (hdr.reserved[0] <= 1 || hdr.reserved[0] == 3) return hdr.reserved[0];
  return 0;
}

uint8_t RecordGeneration(const DataRecordHeader& hdr) {
  const uint8_t codec = RecordCodec(hdr);
  if (codec <= 1 || codec == 3) {
    if (hdr.reserved[1] != 0 || hdr.reserved[2] != 0) return hdr.reserved[1];
  }
  if (codec <= 1) return 0;
  return hdr.reserved[0];
}

Status DecodeRecordValue(const DataRecordHeader& hdr, std::string payload,
                         std::string* out) {
  if (!out) return Status::InvalidArgument("out is null");
  const uint8_t wire = RecordCodec(hdr);
  if (!payload.empty() && wire == 1) {
    if (payload.size() < 4) return Status::CorruptPage("compressed value trunc");
    const auto* u = reinterpret_cast<const unsigned char*>(payload.data());
    const uint32_t usize = static_cast<uint32_t>(u[0]) |
                           (static_cast<uint32_t>(u[1]) << 8) |
                           (static_cast<uint32_t>(u[2]) << 16) |
                           (static_cast<uint32_t>(u[3]) << 24);
    return DecompressValue(ValueCodec::kLegacyRle, payload.substr(4), usize, out);
  }
  if (!payload.empty() && wire == 3) {
    if (payload.size() < 4) return Status::CorruptPage("compressed value trunc");
    const auto* u = reinterpret_cast<const unsigned char*>(payload.data());
    const uint32_t usize = static_cast<uint32_t>(u[0]) |
                           (static_cast<uint32_t>(u[1]) << 8) |
                           (static_cast<uint32_t>(u[2]) << 16) |
                           (static_cast<uint32_t>(u[3]) << 24);
    return DecompressValue(ValueCodec::kLzma7z, payload, usize, out);
  }
  *out = std::move(payload);
  return Status::Ok();
}

}  // namespace

void DataFile::SetCompressValues(bool enable) { compress_values_ = enable; }
DataFile::DataFile(std::string path) : path_(std::move(path)) {
  std::filesystem::create_directories(
      std::filesystem::path(path_).parent_path());
}

Status DataFile::EnsureAppendOpen() const {
  if (append_open_) return Status::Ok();
  append_stream_.open(path_, std::ios::binary | std::ios::out | std::ios::app);
  if (!append_stream_) return Status::IoError("datafile append open failed");
  append_open_ = true;
  return Status::Ok();
}

void DataFile::FlushAppendStream() {
  if (append_open_) append_stream_.flush();
}

Status DataFile::Append(uint64_t lsn, const std::string& key,
                        const std::string& value, bool deleted,
                        uint8_t generation) {
  const Status open_st = EnsureAppendOpen();
  if (!open_st.ok()) return open_st;

  const auto offset = static_cast<uint64_t>(append_stream_.tellp());

  std::string stored_value = value;
  uint8_t codec = 0;
  if (!deleted && compress_values_ && !value.empty()) {
    ValueCodecResult cr{};
    const Status cs = CompressValue(value, true, &cr);
    if (cs.ok() && cr.codec == ValueCodec::kLzma7z) {
      codec = 3;
      stored_value = cr.payload;
    }
  }

  DataRecordHeader hdr{};
  hdr.lsn = lsn;
  hdr.key_len = static_cast<uint16_t>(key.size());
  hdr.value_len = static_cast<uint16_t>(stored_value.size());
  hdr.deleted = deleted ? 1 : 0;
  hdr.reserved[0] = codec;
  hdr.reserved[1] = generation;
  hdr.record_crc = Crc32(&hdr, sizeof(hdr) - sizeof(hdr.record_crc));

  append_stream_.write(reinterpret_cast<const char*>(&hdr), sizeof(hdr));
  if (!key.empty()) {
    append_stream_.write(key.data(), static_cast<std::streamsize>(key.size()));
  }
  if (!stored_value.empty()) {
    append_stream_.write(stored_value.data(),
                         static_cast<std::streamsize>(stored_value.size()));
  }
  if (!append_stream_) return Status::IoError("datafile append write failed");
  lsn_index_.Update(offset, lsn);
  return Status::Ok();
}

Status DataFile::EnsureReadOpen() const {
  if (read_open_) return Status::Ok();
  read_stream_.open(path_, std::ios::binary);
  if (!read_stream_) return Status::IoError("datafile read open failed");
  read_open_ = true;
  return Status::Ok();
}

Status DataFile::ParseRecordAt(const uint8_t* base, size_t len,
                               uint64_t record_offset, std::string* key,
                               std::string* value, uint64_t* lsn, bool* deleted,
                               uint8_t reclaim_generation) {
  if (record_offset + sizeof(DataRecordHeader) > len) {
    return Status::IoError("datafile record out of view");
  }
  DataRecordHeader hdr{};
  std::memcpy(&hdr, base + record_offset, sizeof(hdr));
  const uint32_t expected = Crc32(&hdr, sizeof(hdr) - sizeof(hdr.record_crc));
  if (hdr.record_crc != expected) return Status::CorruptPage("datafile crc mismatch");
  const uint8_t gen = RecordGeneration(hdr);
  if (reclaim_generation != 0xFF && gen == reclaim_generation) {
    return Status::NotFound("reclaimed generation");
  }
  const size_t need = sizeof(DataRecordHeader) + hdr.key_len + hdr.value_len;
  if (record_offset + need > len) return Status::IoError("datafile record truncated");
  std::string k(hdr.key_len, '\0');
  std::string payload(hdr.value_len, '\0');
  size_t pos = static_cast<size_t>(record_offset) + sizeof(DataRecordHeader);
  if (hdr.key_len) std::memcpy(k.data(), base + pos, hdr.key_len);
  pos += hdr.key_len;
  if (hdr.value_len) std::memcpy(payload.data(), base + pos, hdr.value_len);
  std::string v;
  const Status ds = DecodeRecordValue(hdr, std::move(payload), &v);
  if (!ds.ok()) return ds;
  if (key) *key = std::move(k);
  if (value) *value = std::move(v);
  if (lsn) *lsn = hdr.lsn;
  if (deleted) *deleted = hdr.deleted != 0;
  return Status::Ok();
}

Status DataFile::BuildLsnIndex() {
  lsn_index_.Clear();
  const Status ls = LoadLsnIndexSidecar();
  if (ls.ok() && lsn_index_.size() > 0) return Status::Ok();
  lsn_index_.Clear();
  return lsn_index_.BuildFromFile(path_);
}

Status DataFile::SaveLsnIndexSidecar() const {
  return lsn_index_.SaveToFile(path_ + ".didx");
}

Status DataFile::LoadLsnIndexSidecar() {
  return lsn_index_.LoadFromFile(path_ + ".didx");
}

Status DataFile::ReadRecordFromView(const uint8_t* base, size_t len,
                                    uint64_t record_offset, std::string* key,
                                    std::string* value, uint64_t* lsn,
                                    bool* deleted,
                                    uint8_t reclaim_generation) const {
  if (!base) return Status::InvalidArgument("null view");
  return ParseRecordAt(base, len, record_offset, key, value, lsn, deleted,
                       reclaim_generation);
}

Status DataFile::ReadRecordAt(uint64_t offset, std::string* key,
                              std::string* value, uint64_t* lsn, bool* deleted,
                              uint8_t reclaim_generation) const {
  if (append_open_) append_stream_.flush();
  const Status open_st = EnsureReadOpen();
  if (!open_st.ok()) return open_st;
  read_stream_.clear();
  read_stream_.seekg(static_cast<std::streamoff>(offset));
  DataRecordHeader hdr{};
  read_stream_.read(reinterpret_cast<char*>(&hdr), sizeof(hdr));
  if (!read_stream_) return Status::IoError("datafile record read failed");
  const uint32_t expected = Crc32(&hdr, sizeof(hdr) - sizeof(hdr.record_crc));
  if (hdr.record_crc != expected) return Status::CorruptPage("datafile crc mismatch");
  if (reclaim_generation != 0xFF &&
      RecordGeneration(hdr) == reclaim_generation) {
    return Status::NotFound("reclaimed generation");
  }
  std::string k(hdr.key_len, '\0');
  std::string payload(hdr.value_len, '\0');
  if (hdr.key_len) read_stream_.read(k.data(), hdr.key_len);
  if (hdr.value_len) read_stream_.read(payload.data(), hdr.value_len);
  std::string v;
  const Status ds = DecodeRecordValue(hdr, std::move(payload), &v);
  if (!ds.ok()) return ds;
  if (key) *key = std::move(k);
  if (value) *value = std::move(v);
  if (lsn) *lsn = hdr.lsn;
  if (deleted) *deleted = hdr.deleted != 0;
  return Status::Ok();
}

Status DataFile::ReadRecordsAtOffsets(
    MmapWindowManager* mmap_mgr,
    const std::vector<std::pair<uint64_t, size_t>>& offset_hit_indices,
    std::vector<std::string>* values_out,
    uint8_t reclaim_generation) const {
  (void)mmap_mgr;
  if (!values_out) return Status::InvalidArgument("invalid batch read");
  const Status open_st = EnsureReadOpen();
  if (!open_st.ok()) return open_st;

  auto sorted = offset_hit_indices;
  std::sort(sorted.begin(), sorted.end(),
            [](const auto& a, const auto& b) { return a.first < b.first; });
  for (const auto& entry : sorted) {
    read_stream_.clear();
    read_stream_.seekg(static_cast<std::streamoff>(entry.first));
    DataRecordHeader hdr{};
    read_stream_.read(reinterpret_cast<char*>(&hdr), sizeof(hdr));
    if (!read_stream_) return Status::IoError("datafile batch record read failed");
    const uint32_t expected = Crc32(&hdr, sizeof(hdr) - sizeof(hdr.record_crc));
    if (hdr.record_crc != expected) return Status::CorruptPage("datafile crc mismatch");
    if (reclaim_generation != 0xFF &&
        RecordGeneration(hdr) == reclaim_generation) {
      continue;
    }
    std::string key(hdr.key_len, '\0');
    std::string payload(hdr.value_len, '\0');
    if (hdr.key_len) {
      read_stream_.read(key.data(), hdr.key_len);
    }
    if (hdr.value_len) {
      read_stream_.read(payload.data(), hdr.value_len);
    }
    if (!read_stream_) return Status::IoError("datafile batch payload read failed");
    if (hdr.deleted) continue;
    std::string value;
    const Status ds = DecodeRecordValue(hdr, std::move(payload), &value);
    if (!ds.ok()) return ds;
    if (entry.second < values_out->size()) {
      (*values_out)[entry.second] = std::move(value);
    }
  }
  return Status::Ok();
}

namespace {

bool GenerationVisible(uint8_t record_gen, uint8_t reclaim_generation) {
  if (reclaim_generation != 0xFF && record_gen == reclaim_generation) {
    return false;
  }
  return true;
}

Status ParseRecordsIncremental(
    const uint8_t* data, size_t len, size_t* consumed, uint64_t base_offset,
    std::unordered_map<std::string, std::pair<std::string, uint64_t>>* out,
    uint64_t* max_lsn, uint64_t lsn_cap, uint64_t byte_cap,
    uint8_t reclaim_generation, bool validate_crc) {
  if (!data && len > 0) return Status::InvalidArgument("null buffer");
  if (!consumed) return Status::InvalidArgument("consumed is null");
  *consumed = 0;
  uint64_t local_max = max_lsn ? *max_lsn : 0;
  size_t pos = 0;
  while (pos < len) {
    if (byte_cap > 0 && base_offset + pos >= byte_cap) break;

    if (pos + sizeof(DataRecordHeader) > len) break;

    DataRecordHeader hdr{};
    std::memcpy(&hdr, data + pos, sizeof(hdr));
    const size_t record_start = pos;
    pos += sizeof(hdr);
    if (byte_cap > 0 && base_offset + record_start + sizeof(hdr) > byte_cap) {
      return Status::CorruptPage("datafile truncated mid-record");
    }

    if (validate_crc) {
      const uint32_t expected =
          Crc32(&hdr, sizeof(hdr) - sizeof(hdr.record_crc));
      if (hdr.record_crc != expected) {
        const size_t skip = hdr.key_len + hdr.value_len;
        if (pos + skip > len) break;
        pos += skip;
        *consumed = pos;
        continue;
      }
    }

    const size_t payload = hdr.key_len + hdr.value_len;
    if (pos + payload > len) break;
    if (byte_cap > 0 && base_offset + pos + payload > byte_cap) {
      return Status::CorruptPage("datafile truncated mid-payload");
    }

    std::string key(hdr.key_len, '\0');
    std::string value(hdr.value_len, '\0');
    if (hdr.key_len) {
      std::memcpy(key.data(), data + pos, hdr.key_len);
      pos += hdr.key_len;
    }
    if (hdr.value_len) {
      std::memcpy(value.data(), data + pos, hdr.value_len);
      pos += hdr.value_len;
    }

    if (lsn_cap > 0 && hdr.lsn > lsn_cap) {
      *consumed = pos;
      continue;
    }
    if (!GenerationVisible(RecordGeneration(hdr), reclaim_generation)) {
      *consumed = pos;
      continue;
    }

    local_max = std::max(local_max, hdr.lsn);
    if (!out) {
      *consumed = pos;
      continue;
    }
    std::string decoded;
    const Status ds = DecodeRecordValue(hdr, std::move(value), &decoded);
    if (!ds.ok()) return ds;
    if (hdr.deleted) {
      out->erase(key);
    } else {
      (*out)[key] = {decoded, hdr.lsn};
    }
    *consumed = pos;
  }
  if (max_lsn) *max_lsn = local_max;
  return Status::Ok();
}

Status ParseRecordsFromBuffer(
    const uint8_t* data, size_t len,
    std::unordered_map<std::string, std::pair<std::string, uint64_t>>* out,
    uint64_t* max_lsn, uint64_t lsn_cap, uint64_t byte_cap,
    uint8_t reclaim_generation, bool validate_crc) {
  size_t consumed = 0;
  return ParseRecordsIncremental(data, len, &consumed, 0, out, max_lsn, lsn_cap,
                                 byte_cap, reclaim_generation, validate_crc);
}

Status LoadRecords(
    const std::string& path,
    std::unordered_map<std::string, std::pair<std::string, uint64_t>>* out,
    uint64_t* max_lsn, uint64_t lsn_cap, uint64_t byte_cap,
    uint8_t reclaim_generation, bool validate_crc) {
  std::ifstream in(path, std::ios::binary);
  if (!in) {
    if (out) out->clear();
    if (max_lsn) *max_lsn = 0;
    return Status::Ok();
  }
  std::vector<uint8_t> buf(static_cast<size_t>(in.seekg(0, std::ios::end).tellg()));
  in.seekg(0);
  in.read(reinterpret_cast<char*>(buf.data()),
          static_cast<std::streamsize>(buf.size()));
  return ParseRecordsFromBuffer(buf.data(), buf.size(), out, max_lsn, lsn_cap,
                                byte_cap, reclaim_generation, validate_crc);
}

}  // namespace

Status DataFile::LoadRecordsFromView(
    const uint8_t* base, size_t len,
    std::unordered_map<std::string, std::pair<std::string, uint64_t>>* out,
    uint64_t* max_lsn, uint64_t lsn_cap, uint64_t byte_cap,
    uint8_t reclaim_generation) const {
  size_t consumed = 0;
  return ParseRecordsIncremental(base, len, &consumed, 0, out, max_lsn, lsn_cap,
                                 byte_cap, reclaim_generation, true);
}

Status DataFile::LoadRecordsIncremental(
    const uint8_t* base, size_t len, size_t* consumed, uint64_t base_file_offset,
    std::unordered_map<std::string, std::pair<std::string, uint64_t>>* out,
    uint64_t* max_lsn, uint64_t lsn_cap, uint64_t byte_cap,
    uint8_t reclaim_generation) const {
  if (!consumed) return Status::InvalidArgument("consumed is null");
  return ParseRecordsIncremental(base, len, consumed, base_file_offset, out,
                                 max_lsn, lsn_cap, byte_cap, reclaim_generation,
                                 true);
}

Status DataFile::LoadAll(
    std::unordered_map<std::string, std::pair<std::string, uint64_t>>* out,
    uint64_t* max_lsn, uint8_t /*visible_generation*/,
    uint8_t reclaim_generation) const {
  return LoadRecords(path_, out, max_lsn, 0, 0, reclaim_generation, true);
}

Status DataFile::LoadUpToLsn(
    uint64_t max_lsn_in,
    std::unordered_map<std::string, std::pair<std::string, uint64_t>>* out,
    uint64_t* max_lsn_out, uint8_t /*visible_generation*/,
    uint8_t reclaim_generation) const {
  return LoadRecords(path_, out, max_lsn_out, max_lsn_in, 0, reclaim_generation,
                     true);
}

Status DataFile::LoadUpToByteOffset(
    uint64_t max_bytes,
    std::unordered_map<std::string, std::pair<std::string, uint64_t>>* out,
    uint64_t* max_lsn, uint8_t /*visible_generation*/,
    uint8_t reclaim_generation) const {
  return LoadRecords(path_, out, max_lsn, 0, max_bytes, reclaim_generation,
                     true);
}

Status DataFile::CorruptRecordAtOffsetForTest(uint64_t offset) {
  std::fstream file(path_, std::ios::binary | std::ios::in | std::ios::out);
  if (!file) return Status::IoError("datafile open failed");
  file.seekg(static_cast<std::streamoff>(offset));
  DataRecordHeader hdr{};
  file.read(reinterpret_cast<char*>(&hdr), sizeof(hdr));
  if (!file) return Status::IoError("datafile read at offset failed");
  hdr.record_crc ^= 0xFFFFFFFFu;
  const auto crc_offset =
      static_cast<std::streamoff>(offset) +
      static_cast<std::streamoff>(offsetof(DataRecordHeader, record_crc));
  file.seekp(crc_offset);
  file.write(reinterpret_cast<const char*>(&hdr.record_crc),
             sizeof(hdr.record_crc));
  if (!file) return Status::IoError("datafile corrupt write failed");
  return Status::Ok();
}

Status DataFile::TruncateToForTest(uint64_t size) {
  std::filesystem::resize_file(path_, static_cast<std::uintmax_t>(size));
  return Status::Ok();
}

std::uintmax_t DataFile::FileSize() const {
  const std::filesystem::path p(path_);
  if (!std::filesystem::exists(p)) return 0;
  return std::filesystem::file_size(p);
}

}  // namespace ebtree
