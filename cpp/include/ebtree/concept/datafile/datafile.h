#pragma once

#include <cstdint>
#include <fstream>
#include <string>
#include <unordered_map>
#include <vector>

#include "ebtree/common/status.h"
#include "ebtree/concept/codec/codec_registry.h"
#include "ebtree/concept/datafile/datafile_lsn_index.h"

namespace ebtree {

struct EngineStats;
class MmapWindowManager;

#pragma pack(push, 1)
struct DataRecordHeader {
  uint64_t lsn{0};
  uint16_t key_len{0};
  uint16_t value_len{0};
  uint8_t deleted{0};
  uint8_t reserved[3]{};
  uint32_t record_crc{0};
};
#pragma pack(pop)

constexpr uint8_t kDataFileGenerationAll = 0xFF;

class DataFile {
 public:
  explicit DataFile(std::string path);

  Status Append(uint64_t lsn, const std::string& key, const std::string& value,
                bool deleted, uint8_t generation = 0);
  void FlushAppendStream();

  Status LoadAll(std::unordered_map<std::string, std::pair<std::string, uint64_t>>* out,
                 uint64_t* max_lsn,
                 uint8_t visible_generation = kDataFileGenerationAll,
                 uint8_t reclaim_generation = 0xFF) const;
  Status LoadUpToLsn(
      uint64_t max_lsn,
      std::unordered_map<std::string, std::pair<std::string, uint64_t>>* out,
      uint64_t* max_lsn_out,
      uint8_t visible_generation = kDataFileGenerationAll,
      uint8_t reclaim_generation = 0xFF) const;
  Status LoadUpToByteOffset(
      uint64_t max_bytes,
      std::unordered_map<std::string, std::pair<std::string, uint64_t>>* out,
      uint64_t* max_lsn,
      uint8_t visible_generation = kDataFileGenerationAll,
      uint8_t reclaim_generation = 0xFF) const;

  Status LoadRecordsFromView(
      const uint8_t* base, size_t len,
      std::unordered_map<std::string, std::pair<std::string, uint64_t>>* out,
      uint64_t* max_lsn, uint64_t lsn_cap = 0, uint64_t byte_cap = 0,
      uint8_t reclaim_generation = 0xFF) const;

  Status LoadRecordsIncremental(
      const uint8_t* base, size_t len, size_t* consumed, uint64_t base_file_offset,
      std::unordered_map<std::string, std::pair<std::string, uint64_t>>* out,
      uint64_t* max_lsn, uint64_t lsn_cap = 0, uint64_t byte_cap = 0,
      uint8_t reclaim_generation = 0xFF) const;

  Status CorruptRecordAtOffsetForTest(uint64_t offset);
  Status TruncateToForTest(uint64_t size);

  Status ReadRecordAt(uint64_t offset, std::string* key, std::string* value,
                      uint64_t* lsn, bool* deleted,
                      uint8_t reclaim_generation = 0xFF) const;
  Status ReadRecordFromView(const uint8_t* base, size_t len,
                            uint64_t record_offset, std::string* key,
                            std::string* value, uint64_t* lsn, bool* deleted,
                            uint8_t reclaim_generation = 0xFF) const;
  Status ReadRecordsAtOffsets(
      MmapWindowManager* mmap_mgr,
      const std::vector<std::pair<uint64_t, size_t>>& offset_hit_indices,
      std::vector<std::string>* values_out,
      uint8_t reclaim_generation = 0xFF) const;
  Status BuildLsnIndex();
  Status SaveLsnIndexSidecar() const;
  Status LoadLsnIndexSidecar();
  const DataFileLsnIndex& lsn_index() const { return lsn_index_; }
  DataFileLsnIndex* lsn_index_mut() { return &lsn_index_; }
  Status ReadHeaderAtLsn(uint64_t lsn, DataRecordHeader* hdr) const;

  std::uintmax_t FileSize() const;

  void SetCompressValues(bool enable);
  void SetCompressPolicy(CompressPolicy policy);
  void SetCompressStats(EngineStats* stats);

  const std::string& path() const { return path_; }

 private:
  std::string path_;
  bool compress_values_{false};
  CompressPolicy compress_policy_{CompressPolicy::kOff};
  EngineStats* compress_stats_{nullptr};
  mutable std::fstream append_stream_;
  mutable std::ifstream read_stream_;
  mutable bool append_open_{false};
  mutable bool read_open_{false};
  DataFileLsnIndex lsn_index_;

  Status EnsureAppendOpen() const;
  Status EnsureReadOpen() const;
  static Status ParseRecordAt(const uint8_t* base, size_t len,
                              uint64_t record_offset, std::string* key,
                              std::string* value, uint64_t* lsn, bool* deleted,
                              uint8_t reclaim_generation,
                              EngineStats* decompress_stats);
};

}  // namespace ebtree
