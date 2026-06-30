#include "catalog_expect.h"

#include <filesystem>
#include <fstream>
#include <set>
#include <sstream>

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

bool ExtractJsonUint(const std::string& line, const std::string& key,
                     uint32_t* out) {
  const std::string needle = "\"" + key + "\":";
  const auto pos = line.find(needle);
  if (pos == std::string::npos) return false;
  *out = static_cast<uint32_t>(
      std::stoul(line.substr(pos + needle.size())));
  return true;
}

uint32_t TableIdFromKey(const std::string& key) {
  const auto pos = key.find(':');
  if (pos == std::string::npos || pos == 0) return 0;
  try {
    return static_cast<uint32_t>(std::stoul(key.substr(0, pos)));
  } catch (...) {
    return 0;
  }
}

}  // namespace

std::vector<std::string> CatalogTableIdPrefixes(const std::string& catalog_path) {
  std::vector<std::string> prefixes;
  if (!std::filesystem::exists(catalog_path)) return prefixes;

  std::ifstream in(catalog_path);
  if (!in) return prefixes;

  std::string content((std::istreambuf_iterator<char>(in)),
                      std::istreambuf_iterator<char>());
  size_t pos = 0;
  while ((pos = content.find("\"id\"", pos)) != std::string::npos) {
    const auto obj_start = content.rfind('{', pos);
    const auto obj_end = content.find('}', pos);
    if (obj_start == std::string::npos || obj_end == std::string::npos) break;
    const std::string obj = content.substr(obj_start, obj_end - obj_start + 1);
    uint32_t id = 0;
    if (ExtractJsonUint(obj, "id", &id) && id > 0) {
      prefixes.push_back(std::to_string(id) + ":");
    }
    pos = obj_end + 1;
  }
  return prefixes;
}

Status LoadCatalogExpectSnapshot(const std::string& catalog_path,
                                 ExpectSnapshot* out) {
  if (!out) return Status::InvalidArgument("out is null");
  if (!std::filesystem::exists(catalog_path)) return Status::Ok();

  const std::vector<std::string> prefixes =
      CatalogTableIdPrefixes(catalog_path);
  if (prefixes.empty()) return Status::Ok();

  std::set<uint32_t> valid_ids;
  for (const auto& prefix : prefixes) {
    valid_ids.insert(TableIdFromKey(prefix));
  }

  if (out->key_set_source.empty()) {
    out->key_set_source = "catalog";
  } else if (out->key_set_source.find("catalog") == std::string::npos) {
    out->key_set_source += "+catalog";
  }

  std::vector<ExpectedKeyEntry> filtered;
  filtered.reserve(out->entries.size());
  for (const auto& entry : out->entries) {
    const uint32_t tid = TableIdFromKey(entry.key);
    if (tid == 0 || valid_ids.count(tid) > 0) {
      filtered.push_back(entry);
    }
  }
  out->entries = std::move(filtered);

  std::vector<std::string> filtered_keys;
  filtered_keys.reserve(out->touched_keys.size());
  for (const auto& key : out->touched_keys) {
    const uint32_t tid = TableIdFromKey(key);
    if (tid == 0 || valid_ids.count(tid) > 0) {
      filtered_keys.push_back(key);
    }
  }
  out->touched_keys = std::move(filtered_keys);

  return Status::Ok();
}

}  // namespace audit
}  // namespace ebtree
