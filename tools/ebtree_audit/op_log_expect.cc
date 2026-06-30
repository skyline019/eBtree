#include "op_log_expect.h"

#include <fstream>
#include <sstream>

#include "digest.h"

namespace ebtree {
namespace audit {

namespace {

bool ExtractJsonString(const std::string& line, const std::string& key,
                       std::string* out) {
  const std::string needle = "\"" + key + "\":";
  const auto pos = line.find(needle);
  if (pos == std::string::npos) return false;
  const auto q1 = line.find('"', pos + needle.size());
  if (q1 == std::string::npos) return false;
  const auto q2 = line.find('"', q1 + 1);
  if (q2 == std::string::npos) return false;
  *out = line.substr(q1 + 1, q2 - q1 - 1);
  return true;
}

bool ExtractJsonBool(const std::string& line, const std::string& key,
                     bool* out) {
  const std::string needle = "\"" + key + "\":";
  const auto pos = line.find(needle);
  if (pos == std::string::npos) return false;
  const auto val = line.substr(pos + needle.size());
  if (val.rfind("true", 0) == 0) {
    *out = true;
    return true;
  }
  if (val.rfind("false", 0) == 0) {
    *out = false;
    return true;
  }
  return false;
}

}  // namespace

Status LoadOpLogExpectSnapshot(const std::string& path, ContractMode mode,
                               ExpectSnapshot* out) {
  if (!out) return Status::InvalidArgument("out is null");
  out->mode = mode;
  out->key_set_source = "op_log";
  out->entries.clear();
  out->touched_keys.clear();

  std::ifstream in(path);
  if (!in) return Status::NotFound("op_log not found: " + path);

  std::string line;
  while (std::getline(in, line)) {
    if (line.empty()) continue;
    std::string op;
    std::string key;
    std::string value_sha256;
    bool durable = false;
    if (!ExtractJsonString(line, "op", &op)) continue;
    if (!ExtractJsonString(line, "key", &key)) continue;
    ExtractJsonString(line, "value_sha256", &value_sha256);
    ExtractJsonBool(line, "durable_at_return", &durable);

    out->touched_keys.push_back(key);
    if (op == "delete") continue;

    ExpectedKeyEntry entry{};
    entry.key = key;
    entry.in_durable_set = durable;
    entry.in_visibility_set = true;
    if (!value_sha256.empty()) {
      entry.expected_value_hash = value_sha256;
    }
    out->entries.push_back(std::move(entry));
  }
  return Status::Ok();
}

std::vector<std::string> CollectProbeKeysFromExpect(
    const ExpectSnapshot& expect) {
  return expect.touched_keys;
}

}  // namespace audit
}  // namespace ebtree
