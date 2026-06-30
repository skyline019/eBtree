#include "ebtree/concept/tlog/tlog.h"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>

namespace ebtree {

namespace {

TimestampSourceFn g_timestamp_source;
bool g_checkpoint_ts_active = false;
uint32_t g_checkpoint_ts = 0;

uint32_t WallClockSeconds() {
  return static_cast<uint32_t>(
      std::chrono::duration_cast<std::chrono::seconds>(
          std::chrono::system_clock::now().time_since_epoch())
          .count());
}

uint32_t NowSeconds() {
  if (g_checkpoint_ts_active) return g_checkpoint_ts;
  if (g_timestamp_source) return g_timestamp_source();
  return WallClockSeconds();
}

}  // namespace

void BeginCheckpointTimestampScope() {
  g_checkpoint_ts_active = true;
  g_checkpoint_ts = g_timestamp_source ? g_timestamp_source() : WallClockSeconds();
}

void EndCheckpointTimestampScope() { g_checkpoint_ts_active = false; }

void SetTimestampSourceForTest(TimestampSourceFn fn) {
  g_timestamp_source = std::move(fn);
}

void ResetTimestampSourceForTest() { g_timestamp_source = nullptr; }

TLogWriter::TLogWriter(std::string path) : path_(std::move(path)) {
  std::filesystem::create_directories(
      std::filesystem::path(path_).parent_path());
}

std::string TLogWriter::IndexPath() const { return path_ + "idx"; }

Status TLogWriter::AppendSnapshot(uint64_t data_lsn, uint64_t datafile_size,
                                  uint64_t wal_lsn, uint64_t* out_tail_offset) {
  TLogSnapshot prev{};
  (void)LatestSnapshot(&prev);

  std::fstream file(path_, std::ios::binary | std::ios::in | std::ios::out |
                                std::ios::app);
  if (!file) {
    std::ofstream create(path_, std::ios::binary);
    create.close();
    file.open(path_, std::ios::binary | std::ios::in | std::ios::out |
                          std::ios::app);
  }
  if (!file) return Status::IoError("tlog open failed");

  file.seekp(0, std::ios::end);
  const auto pos = static_cast<uint32_t>(file.tellp());
  const uint32_t ts = NowSeconds();

  TLogEntry entry{};
  entry.page_offset = datafile_size;
  entry.timestamp_sec = ts;
  entry.prev_offset = prev.file_offset > 0 ? static_cast<uint32_t>(prev.file_offset)
                                           : 0;

  file.write(reinterpret_cast<const char*>(&entry), sizeof(entry));
  file.flush();
  if (!file) return Status::IoError("tlog append failed");

  TLogIndexEntry idx{};
  idx.data_lsn = data_lsn;
  idx.wal_lsn = wal_lsn;
  idx.datafile_size = datafile_size;
  idx.timestamp_sec = ts;
  idx.file_offset = pos;

  std::fstream idx_file(IndexPath(),
                        std::ios::binary | std::ios::in | std::ios::out |
                            std::ios::app);
  if (!idx_file) {
    std::ofstream create_idx(IndexPath(), std::ios::binary);
    create_idx.close();
    idx_file.open(IndexPath(), std::ios::binary | std::ios::in | std::ios::out |
                                   std::ios::app);
  }
  if (!idx_file) return Status::IoError("tlogidx open failed");
  idx_file.write(reinterpret_cast<const char*>(&idx), sizeof(idx));
  idx_file.flush();
  if (!idx_file) return Status::IoError("tlogidx append failed");

  if (out_tail_offset) *out_tail_offset = pos;
  return Status::Ok();
}

Status TLogWriter::LatestSnapshot(TLogSnapshot* out) const {
  if (!out) return Status::InvalidArgument("out is null");
  *out = {};

  std::ifstream idx(IndexPath(), std::ios::binary);
  if (!idx) return Status::Ok();

  idx.seekg(0, std::ios::end);
  const auto end = idx.tellg();
  if (end < static_cast<std::streamoff>(sizeof(TLogIndexEntry))) {
    return Status::Ok();
  }

  TLogIndexEntry latest{};
  idx.seekg(-static_cast<std::streamoff>(sizeof(TLogIndexEntry)), std::ios::end);
  idx.read(reinterpret_cast<char*>(&latest), sizeof(latest));
  if (!idx) return Status::IoError("tlogidx read failed");

  out->data_lsn = latest.data_lsn;
  out->datafile_size = latest.datafile_size;
  out->wal_lsn = latest.wal_lsn;
  out->file_offset = latest.file_offset;
  out->timestamp_sec = latest.timestamp_sec;
  return Status::Ok();
}

TLogReader::TLogReader(std::string tlog_path)
    : tlog_path_(std::move(tlog_path)), index_path_(tlog_path_ + "idx") {}

Status TLogReader::ListSnapshots(std::vector<TLogSnapshot>* out) const {
  if (!out) return Status::InvalidArgument("out is null");
  out->clear();

  std::ifstream idx(index_path_, std::ios::binary);
  if (!idx) return Status::Ok();

  TLogIndexEntry entry{};
  while (idx.read(reinterpret_cast<char*>(&entry), sizeof(entry))) {
    TLogSnapshot snap{};
    snap.data_lsn = entry.data_lsn;
    snap.datafile_size = entry.datafile_size;
    snap.wal_lsn = entry.wal_lsn;
    snap.file_offset = entry.file_offset;
    snap.timestamp_sec = entry.timestamp_sec;
    out->push_back(snap);
  }
  return Status::Ok();
}

Status TLogReader::FindSnapshotAt(uint32_t timestamp_sec,
                                  TLogSnapshot* out) const {
  if (!out) return Status::InvalidArgument("out is null");
  *out = {};

  std::vector<TLogSnapshot> snaps;
  const Status ls = ListSnapshots(&snaps);
  if (!ls.ok()) return ls;
  if (snaps.empty()) return Status::NotFound("no tlog snapshots");

  const TLogSnapshot* best = nullptr;
  for (const auto& snap : snaps) {
    if (snap.timestamp_sec <= timestamp_sec) {
      if (!best || snap.timestamp_sec >= best->timestamp_sec) {
        best = &snap;
      }
    }
  }
  if (!best) return Status::NotFound("no snapshot at timestamp");
  *out = *best;
  return Status::Ok();
}

}  // namespace ebtree
