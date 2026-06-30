#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "ebtree/common/status.h"

namespace ebtree {

#pragma pack(push, 1)
struct TLogEntry {
  uint64_t page_offset{0};
  uint32_t timestamp_sec{0};
  uint32_t prev_offset{0};
};
#pragma pack(pop)

static_assert(sizeof(TLogEntry) == 16, "TLogEntry must be 16 bytes");

#pragma pack(push, 1)
struct TLogIndexEntry {
  uint64_t data_lsn{0};
  uint64_t wal_lsn{0};
  uint64_t datafile_size{0};
  uint32_t timestamp_sec{0};
  uint32_t file_offset{0};
};
#pragma pack(pop)

static_assert(sizeof(TLogIndexEntry) == 32, "TLogIndexEntry must be 32 bytes");

struct TLogSnapshot {
  uint64_t data_lsn{0};
  uint64_t datafile_size{0};
  uint64_t wal_lsn{0};
  uint64_t file_offset{0};
  uint32_t timestamp_sec{0};
};

using TimestampSourceFn = std::function<uint32_t()>;

void SetTimestampSourceForTest(TimestampSourceFn fn);
void ResetTimestampSourceForTest();

// One logical Engine::Checkpoint uses a single timestamp for all shard T-Log appends.
void BeginCheckpointTimestampScope();
void EndCheckpointTimestampScope();

class TLogWriter {
 public:
  explicit TLogWriter(std::string path);

  Status AppendSnapshot(uint64_t data_lsn, uint64_t datafile_size,
                        uint64_t wal_lsn, uint64_t* out_tail_offset);
  Status LatestSnapshot(TLogSnapshot* out) const;

  const std::string& path() const { return path_; }
  std::string IndexPath() const;

 private:
  std::string path_;
};

class TLogReader {
 public:
  explicit TLogReader(std::string tlog_path);

  Status ListSnapshots(std::vector<TLogSnapshot>* out) const;
  Status FindSnapshotAt(uint32_t timestamp_sec, TLogSnapshot* out) const;

 private:
  std::string tlog_path_;
  std::string index_path_;
};

}  // namespace ebtree
