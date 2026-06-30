#include "op_log_writer.h"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <vector>

namespace ebtree {
namespace sql {

namespace {

int64_t NowUnix() {
  return std::chrono::duration_cast<std::chrono::seconds>(
             std::chrono::system_clock::now().time_since_epoch())
      .count();
}

std::string JsonEscape(const std::string& s) {
  std::string out;
  out.reserve(s.size() + 8);
  out.push_back('"');
  for (char c : s) {
    if (c == '"') out += "\\\"";
    else if (c == '\\') out += "\\\\";
    else out.push_back(c);
  }
  out.push_back('"');
  return out;
}

}  // namespace

OpLogWriter::OpLogWriter(std::string path) : path_(std::move(path)) {
  const std::filesystem::path p(path_);
  if (p.has_parent_path()) {
    std::filesystem::create_directories(p.parent_path());
  }
  stream_.open(path_, std::ios::app | std::ios::out);
}

Status OpLogWriter::AppendEntry(const OpLogEntry& entry) {
  std::lock_guard<std::mutex> lock(mu_);
  if (!stream_.is_open()) {
    stream_.open(path_, std::ios::app | std::ios::out);
    if (!stream_) return Status::IoError("cannot open op_log: " + path_);
  }
  stream_ << "{"
          << "\"v\":" << entry.version << ","
          << "\"op\":" << JsonEscape(entry.op) << ","
          << "\"key\":" << JsonEscape(entry.key) << ","
          << "\"value_sha256\":" << JsonEscape(entry.value_sha256) << ","
          << "\"lsn\":" << entry.lsn << ","
          << "\"durable_at_return\":" << (entry.durable_at_return ? "true" : "false")
          << ","
          << "\"tier\":";
  switch (entry.tier) {
    case DurabilityClass::kSync:
      stream_ << "\"sync\"";
      break;
    case DurabilityClass::kGroup:
      stream_ << "\"group\"";
      break;
    case DurabilityClass::kBalanced:
    default:
      stream_ << "\"balanced\"";
      break;
  }
  stream_ << ",\"ts_unix\":" << entry.ts_unix << "}\n";
  if (!stream_) return Status::IoError("op_log write failed");
  stream_.flush();
  return Status::Ok();
}

Status OpLogWriter::AppendPut(const std::string& key,
                              const std::string& value_sha256, uint64_t lsn,
                              bool durable_at_return, DurabilityClass tier) {
  OpLogEntry e{};
  e.op = "put";
  e.key = key;
  e.value_sha256 = value_sha256;
  e.lsn = lsn;
  e.durable_at_return = durable_at_return;
  e.tier = tier;
  e.ts_unix = NowUnix();
  return AppendEntry(e);
}

Status OpLogWriter::AppendDelete(const std::string& key, uint64_t lsn,
                                 bool durable_at_return, DurabilityClass tier) {
  OpLogEntry e{};
  e.op = "delete";
  e.key = key;
  e.lsn = lsn;
  e.durable_at_return = durable_at_return;
  e.tier = tier;
  e.ts_unix = NowUnix();
  return AppendEntry(e);
}

Status OpLogWriter::MarkDurableThroughLsn(uint64_t lsn) {
  std::lock_guard<std::mutex> lock(mu_);
  if (stream_.is_open()) stream_.close();

  std::ifstream in(path_);
  if (!in) return Status::Ok();
  std::vector<std::string> lines;
  std::string line;
  while (std::getline(in, line)) {
    if (line.empty()) continue;
    const auto lsn_pos = line.find("\"lsn\":");
    const auto dur_pos = line.find("\"durable_at_return\":false");
    if (lsn_pos != std::string::npos && dur_pos != std::string::npos) {
      uint64_t entry_lsn = 0;
      try {
        entry_lsn = std::stoull(line.substr(lsn_pos + 6));
      } catch (...) {
      }
      if (entry_lsn <= lsn) {
        size_t pos = 0;
        while ((pos = line.find("\"durable_at_return\":false", pos)) !=
               std::string::npos) {
          line.replace(pos, 25, "\"durable_at_return\":true");
          pos += 26;
        }
      }
    }
    lines.push_back(line);
  }
  in.close();

  std::ofstream out(path_, std::ios::trunc);
  if (!out) return Status::IoError("cannot rewrite op_log: " + path_);
  for (const auto& l : lines) out << l << '\n';
  stream_.open(path_, std::ios::app | std::ios::out);
  return Status::Ok();
}

Status OpLogWriter::Flush() {
  std::lock_guard<std::mutex> lock(mu_);
  if (stream_.is_open()) stream_.flush();
  return Status::Ok();
}

}  // namespace sql
}  // namespace ebtree
