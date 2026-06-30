#include "sidecar_stats.h"

#include <fstream>

#include "catalog_expect.h"

namespace ebtree {
namespace audit {

Status CollectOpLogSidecarStats(const std::string& path,
                                OpLogSidecarReport* out) {
  if (!out) return Status::InvalidArgument("out is null");
  out->path = path;
  out->key_set_source = "op_log";
  std::ifstream in(path);
  if (!in) return Status::NotFound("op_log not found: " + path);
  std::string line;
  while (std::getline(in, line)) {
    if (line.empty()) continue;
    ++out->entry_count;
    if (line.find("\"durable_at_return\":true") != std::string::npos) {
      ++out->durable_entry_count;
    } else if (line.find("\"durable_at_return\":false") != std::string::npos) {
      ++out->pending_count;
    }
  }
  return Status::Ok();
}

Status CollectCatalogSidecarStats(const std::string& path,
                                  CatalogSidecarReport* out) {
  if (!out) return Status::InvalidArgument("out is null");
  out->path = path;
  out->key_set_source = "catalog";
  const auto prefixes = CatalogTableIdPrefixes(path);
  out->table_count = static_cast<uint32_t>(prefixes.size());
  return Status::Ok();
}

Status LoadExpectFromJson(const std::string& path, ContractMode mode,
                          ExpectSnapshot* out) {
  if (!out) return Status::InvalidArgument("out is null");
  out->mode = mode;
  out->key_set_source = "expect_json";
  out->entries.clear();
  out->touched_keys.clear();

  std::ifstream in(path);
  if (!in) return Status::NotFound("expect file not found: " + path);

  std::string content((std::istreambuf_iterator<char>(in)),
                      std::istreambuf_iterator<char>());
  size_t pos = 0;
  while ((pos = content.find("\"key\"", pos)) != std::string::npos) {
    const auto q1 = content.find('"', pos + 5);
    if (q1 == std::string::npos) break;
    const auto q2 = content.find('"', q1 + 1);
    if (q2 == std::string::npos) break;
    ExpectedKeyEntry entry{};
    entry.key = content.substr(q1 + 1, q2 - q1 - 1);
    entry.in_durable_set = true;
    entry.in_visibility_set = true;
    out->entries.push_back(entry);
    out->touched_keys.push_back(entry.key);
    pos = q2 + 1;
  }
  return Status::Ok();
}

}  // namespace audit
}  // namespace ebtree
