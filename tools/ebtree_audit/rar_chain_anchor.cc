#include "rar_chain_anchor.h"

#include "rar_chain.h"
#include "rar_sign.h"

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace ebtree {
namespace audit {

namespace {

std::string JsonEscape(const std::string& s) {
  std::string out;
  for (char c : s) {
    if (c == '\\') out += "\\\\";
    else if (c == '"') out += "\\\"";
    else out.push_back(c);
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
      value.push_back(line[i + 1]);
      i += 2;
      continue;
    }
    value.push_back(line[i++]);
  }
  *out = std::move(value);
  return true;
}

bool ExtractJsonUint64Field(const std::string& line, const std::string& key,
                            uint64_t* out) {
  const std::string needle = "\"" + key + "\":";
  const auto pos = line.find(needle);
  if (pos == std::string::npos) return false;
  *out = std::strtoull(line.c_str() + pos + needle.size(), nullptr, 10);
  return true;
}

bool ExtractJsonInt64Field(const std::string& line, const std::string& key,
                           int64_t* out) {
  const std::string needle = "\"" + key + "\":";
  const auto pos = line.find(needle);
  if (pos == std::string::npos) return false;
  *out = std::strtoll(line.c_str() + pos + needle.size(), nullptr, 10);
  return true;
}

CarlSignedTreeHead ParseSthLine(const std::string& line) {
  CarlSignedTreeHead sth{};
  (void)ExtractJsonUint64Field(line, "chain_sequence", &sth.chain_sequence);
  (void)ExtractJsonStringField(line, "root_hash", &sth.root_hash);
  (void)ExtractJsonInt64Field(line, "published_at_unix", &sth.published_at_unix);
  (void)ExtractJsonStringField(line, "signature", &sth.signature);
  (void)ExtractJsonStringField(line, "chain_path", &sth.chain_path);
  return sth;
}

std::string SthToJsonLine(const CarlSignedTreeHead& sth) {
  std::ostringstream oss;
  oss << "{\"chain_sequence\":" << sth.chain_sequence << ",\"root_hash\":\""
      << JsonEscape(sth.root_hash) << "\",\"published_at_unix\":"
      << sth.published_at_unix << ",\"chain_path\":\""
      << JsonEscape(sth.chain_path) << "\"";
  if (!sth.signature.empty()) {
    oss << ",\"signature\":\"" << JsonEscape(sth.signature) << "\"";
  }
  oss << "}";
  return oss.str();
}

std::string SthUnsignedJsonLine(const CarlSignedTreeHead& sth) {
  CarlSignedTreeHead unsigned_sth = sth;
  unsigned_sth.signature.clear();
  return SthToJsonLine(unsigned_sth);
}

std::string ChainBasename(const std::string& chain_path) {
  return std::filesystem::path(chain_path).filename().string();
}

std::string SthPathForChain(const std::string& anchor_dir,
                            const std::string& chain_path) {
  return (std::filesystem::path(anchor_dir) /
          (ChainBasename(chain_path) + ".sth.jsonl"))
      .string();
}

}  // namespace

std::string DefaultCarlAnchorDir(const std::string& engine_path) {
  if (const char* env = std::getenv("EBTREE_CARL_ANCHOR_PATH")) {
    if (env[0] != '\0') return env;
  }
  return engine_path + "/carl_anchors";
}

Status PublishCarlAnchor(const std::string& chain_path,
                         const std::string& anchor_dir, CarlSignedTreeHead* out) {
  if (!out) return Status::InvalidArgument("out is null");
  RarChainEntry last{};
  bool found = false;
  const Status rs = ReadLastRarChainEntry(chain_path, &last, &found);
  if (!rs.ok()) return rs;
  if (!found) return Status::NotFound("empty chain");

  CarlSignedTreeHead sth{};
  sth.chain_sequence = last.sequence;
  sth.root_hash = last.rar_sha256;
  sth.published_at_unix =
      std::chrono::duration_cast<std::chrono::seconds>(
          std::chrono::system_clock::now().time_since_epoch())
          .count();
  sth.chain_path = chain_path;

  const char* key = std::getenv("EBTREE_RAR_KEY");
  if (key && key[0] != '\0') {
    const std::string body = SthUnsignedJsonLine(sth);
    (void)SignRarJson(body, key, &sth.signature);
  }

  std::error_code ec;
  std::filesystem::create_directories(anchor_dir, ec);

  const std::string sth_path = SthPathForChain(anchor_dir, chain_path);
  std::ofstream stream(sth_path, std::ios::app);
  if (!stream) return Status::IoError("anchor open failed: " + sth_path);
  stream << SthToJsonLine(sth) << "\n";
  if (!stream) return Status::IoError("anchor append failed");

  *out = sth;
  return Status::Ok();
}

Status LoadLatestCarlAnchor(const std::string& anchor_dir,
                            const std::string& chain_basename,
                            CarlSignedTreeHead* out, bool* found) {
  if (!out || !found) return Status::InvalidArgument("null out/found");
  *found = false;
  const std::string sth_path =
      (std::filesystem::path(anchor_dir) / (chain_basename + ".sth.jsonl"))
          .string();

  std::ifstream in(sth_path);
  if (!in) return Status::Ok();

  std::string line;
  CarlSignedTreeHead latest{};
  while (std::getline(in, line)) {
    if (line.empty()) continue;
    latest = ParseSthLine(line);
    *found = true;
  }
  if (*found) *out = latest;
  return Status::Ok();
}

Status VerifyCarlAnchorAgainstChain(const std::string& chain_path,
                                    const CarlSignedTreeHead& anchor,
                                    std::string* mismatch_reason) {
  RarChainEntry last{};
  bool found = false;
  const Status rs = ReadLastRarChainEntry(chain_path, &last, &found);
  if (!rs.ok()) return rs;
  if (!found) {
    if (mismatch_reason) *mismatch_reason = "empty chain";
    return Status::Corrupt("empty chain");
  }
  if (last.sequence < anchor.chain_sequence) {
    if (mismatch_reason) {
      *mismatch_reason = "chain shorter than anchor sequence";
    }
    return Status::Corrupt("chain behind anchor");
  }
  if (last.rar_sha256 != anchor.root_hash) {
    if (mismatch_reason) {
      *mismatch_reason = "root_hash mismatch (possible rewrite)";
    }
    return Status::Corrupt("anchor root_hash mismatch");
  }
  return Status::Ok();
}

Status VerifyCarlAnchorSignature(const CarlSignedTreeHead& anchor,
                                 const std::string& pubkey) {
  if (anchor.signature.empty()) {
    return Status::Corrupt("anchor missing signature");
  }
  const std::string body = SthUnsignedJsonLine(anchor);
  return VerifyRarSignature(body, anchor.signature, pubkey);
}

Status VerifyCarlAnchorRequired(const std::string& chain_path,
                                const std::string& anchor_dir,
                                bool require_signature) {
  CarlSignedTreeHead anchor{};
  bool found = false;
  const Status ls = LoadLatestCarlAnchor(anchor_dir, ChainBasename(chain_path),
                                         &anchor, &found);
  if (!ls.ok()) return ls;
  if (!found) return Status::NotFound("no anchor published");
  std::string reason;
  const Status vs = VerifyCarlAnchorAgainstChain(chain_path, anchor, &reason);
  if (!vs.ok()) return vs;

  if (require_signature) {
    const char* key = std::getenv("EBTREE_RAR_KEY");
    if (!key || key[0] == '\0') {
      return Status::InvalidArgument("EBTREE_RAR_KEY required for signature verify");
    }
    return VerifyCarlAnchorSignature(anchor, key);
  }
  if (const char* key = std::getenv("EBTREE_RAR_KEY")) {
    if (key[0] != '\0' && !anchor.signature.empty()) {
      const Status ss = VerifyCarlAnchorSignature(anchor, key);
      if (!ss.ok()) return ss;
    }
  }
  return Status::Ok();
}

void MaybeAutoPublishCarlAnchor(const std::string& chain_path) {
  const char* env = std::getenv("EBTREE_CARL_ANCHOR_PATH");
  if (!env || env[0] == '\0') return;
  CarlSignedTreeHead sth{};
  (void)PublishCarlAnchor(chain_path, env, &sth);
}

}  // namespace audit
}  // namespace ebtree
