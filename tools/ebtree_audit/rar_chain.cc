#include "rar_chain.h"

#include "digest.h"

#include "ebtree/common/status.h"

#include <fstream>
#include <sstream>

namespace ebtree {
namespace audit {

namespace {

std::string JsonEscape(const std::string& s) {
  std::string out;
  out.reserve(s.size() + 8);
  for (char c : s) {
    if (c == '\\') {
      out.append("\\\\");
    } else if (c == '"') {
      out.append("\\\"");
    } else if (c == '\n') {
      out.append("\\n");
    } else if (c == '\r') {
      out.append("\\r");
    } else {
      out.push_back(c);
    }
  }
  return out;
}

bool ExtractJsonStringField(const std::string& line, const std::string& key,
                            std::string* out) {
  const std::string needle = "\"" + key + "\":\"";
  const auto pos = line.find(needle);
  if (pos == std::string::npos) return false;
  size_t i = pos + needle.size();
  std::string value;
  while (i < line.size()) {
    if (line[i] == '"') break;
    if (line[i] == '\\' && i + 1 < line.size()) {
      const char next = line[i + 1];
      if (next == '\\' || next == '"') {
        value.push_back(next);
        i += 2;
        continue;
      }
      if (next == 'n') {
        value.push_back('\n');
        i += 2;
        continue;
      }
      if (next == 'r') {
        value.push_back('\r');
        i += 2;
        continue;
      }
    }
    value.push_back(line[i]);
    ++i;
  }
  *out = std::move(value);
  return true;
}

bool ExtractJsonUint64Field(const std::string& line, const std::string& key,
                            uint64_t* out) {
  const std::string needle = "\"" + key + "\":";
  const auto pos = line.find(needle);
  if (pos == std::string::npos) return false;
  const auto start = pos + needle.size();
  *out = std::strtoull(line.c_str() + start, nullptr, 10);
  return true;
}

bool ExtractJsonInt64Field(const std::string& line, const std::string& key,
                             int64_t* out) {
  const std::string needle = "\"" + key + "\":";
  const auto pos = line.find(needle);
  if (pos == std::string::npos) return false;
  const auto start = pos + needle.size();
  *out = std::strtoll(line.c_str() + start, nullptr, 10);
  return true;
}

RarChainEntry ParseChainLine(const std::string& line) {
  RarChainEntry entry{};
  ExtractJsonUint64Field(line, "sequence", &entry.sequence);
  ExtractJsonUint64Field(line, "checkpoint_lsn", &entry.checkpoint_lsn);
  ExtractJsonStringField(line, "rar_sha256", &entry.rar_sha256);
  ExtractJsonStringField(line, "prev_rar_sha256", &entry.prev_rar_sha256);
  ExtractJsonStringField(line, "op_log_head_sha256", &entry.op_log_head_sha256);
  ExtractJsonInt64Field(line, "generated_at_unix", &entry.generated_at_unix);
  ExtractJsonStringField(line, "body_json", &entry.body_json);
  if (entry.body_json.empty()) {
    entry.body_json = line;
  }
  return entry;
}

}  // namespace

Status AppendRarChainEntry(const std::string& chain_path,
                           const RarChainEntry& entry) {
  std::ofstream stream(chain_path, std::ios::app);
  if (!stream) return Status::IoError("rar chain open failed");
  stream << "{\"sequence\":" << entry.sequence
         << ",\"checkpoint_lsn\":" << entry.checkpoint_lsn
         << ",\"rar_sha256\":\"" << JsonEscape(entry.rar_sha256)
         << "\",\"prev_rar_sha256\":\"" << JsonEscape(entry.prev_rar_sha256)
         << "\",\"op_log_head_sha256\":\"" << JsonEscape(entry.op_log_head_sha256)
         << "\",\"generated_at_unix\":" << entry.generated_at_unix
         << ",\"body_json\":\"" << JsonEscape(entry.body_json) << "\"";
  if (!entry.signature.empty()) {
    stream << ",\"signature\":\"" << JsonEscape(entry.signature) << "\"";
  }
  stream << "}\n";
  if (!stream) return Status::IoError("rar chain append failed");
  return Status::Ok();
}

Status ReadRarChainEntries(const std::string& chain_path,
                           std::vector<RarChainEntry>* out) {
  if (!out) return Status::InvalidArgument("out is null");
  out->clear();
  std::ifstream in(chain_path);
  if (!in) return Status::NotFound("rar chain not found: " + chain_path);
  std::string line;
  while (std::getline(in, line)) {
    if (line.empty()) continue;
    out->push_back(ParseChainLine(line));
  }
  return Status::Ok();
}

Status ReadLastRarChainEntry(const std::string& chain_path, RarChainEntry* out,
                             bool* found) {
  if (!out || !found) return Status::InvalidArgument("null out/found");
  *found = false;
  std::ifstream in(chain_path, std::ios::binary);
  if (!in) return Status::Ok();

  in.seekg(0, std::ios::end);
  std::streamoff end = in.tellg();
  if (end <= 0) return Status::Ok();

  std::string line;
  for (std::streamoff pos = end - 1; pos >= 0; --pos) {
    in.seekg(pos);
    char c = '\0';
    in.get(c);
    if (c == '\n') {
      if (!line.empty()) break;
      continue;
    }
    if (c != '\r') {
      line.insert(line.begin(), c);
    }
  }
  if (line.empty()) return Status::Ok();
  *out = ParseChainLine(line);
  *found = true;
  return Status::Ok();
}

Status VerifyRarChain(const std::string& chain_path,
                      RarChainVerifyReport* out) {
  if (!out) return Status::InvalidArgument("out is null");
  out->errors.clear();
  out->consistent = true;
  out->entry_count = 0;
  out->last_sequence = 0;
  out->last_rar_sha256.clear();

  std::vector<RarChainEntry> entries;
  const Status rs = ReadRarChainEntries(chain_path, &entries);
  if (!rs.ok()) return rs;

  std::string prev;
  uint64_t expect_seq = 1;
  for (const auto& entry : entries) {
    ++out->entry_count;
    if (entry.sequence != expect_seq) {
      out->consistent = false;
      out->errors.push_back("sequence gap at " + std::to_string(entry.sequence));
    }
    if (!prev.empty() && entry.prev_rar_sha256 != prev) {
      out->consistent = false;
      out->errors.push_back("prev_rar_sha256 mismatch at seq " +
                            std::to_string(entry.sequence));
    }
    if (!entry.body_json.empty()) {
      const std::string body = entry.body_json;
      const std::string hash = Sha256HexString(body);
      if (!entry.rar_sha256.empty() && hash != entry.rar_sha256) {
        out->consistent = false;
        out->errors.push_back("rar_sha256 mismatch at seq " +
                              std::to_string(entry.sequence));
      }
    }
    prev = entry.rar_sha256;
    out->last_sequence = entry.sequence;
    out->last_rar_sha256 = entry.rar_sha256;
    ++expect_seq;
  }
  return Status::Ok();
}

std::string RarChainLastSha256(const std::string& chain_path) {
  RarChainEntry last{};
  bool found = false;
  if (!ReadLastRarChainEntry(chain_path, &last, &found).ok() || !found) {
    return {};
  }
  return last.rar_sha256;
}

uint64_t RarChainNextSequence(const std::string& chain_path) {
  RarChainEntry last{};
  bool found = false;
  if (!ReadLastRarChainEntry(chain_path, &last, &found).ok() || !found) {
    return 1;
  }
  return last.sequence + 1;
}

}  // namespace audit
}  // namespace ebtree
