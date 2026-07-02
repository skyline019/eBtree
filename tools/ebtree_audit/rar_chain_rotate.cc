#include "rar_chain_rotate.h"

#include "rar_chain.h"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace ebtree {
namespace audit {

uint64_t RarChainEntryCount(const std::string& chain_path) {
  std::vector<RarChainEntry> entries;
  if (!ReadRarChainEntries(chain_path, &entries).ok()) return 0;
  return entries.size();
}

Status RotateRarChainIfNeeded(const std::string& chain_path,
                              uint64_t max_entries) {
  if (max_entries == 0 || chain_path.empty()) return Status::Ok();
  RarChainEntry last{};
  bool found = false;
  if (!ReadLastRarChainEntry(chain_path, &last, &found).ok() || !found) {
    return Status::Ok();
  }
  if (last.sequence < max_entries) return Status::Ok();

  const auto now = std::chrono::duration_cast<std::chrono::seconds>(
                       std::chrono::system_clock::now().time_since_epoch())
                       .count();
  const std::filesystem::path src(chain_path);
  const std::filesystem::path dst =
      src.parent_path() /
      (src.stem().string() + "." + std::to_string(now) + ".bak.jsonl");

  std::error_code ec;
  std::filesystem::rename(src, dst, ec);
  if (ec) return Status::IoError("rar chain rotate failed: " + ec.message());
  return Status::Ok();
}

}  // namespace audit
}  // namespace ebtree
