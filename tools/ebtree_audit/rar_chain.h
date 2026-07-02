#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "ebtree/common/status.h"

namespace ebtree {
namespace audit {

struct RarChainEntry {
  uint64_t sequence{0};
  uint64_t checkpoint_lsn{0};
  std::string kernel_json;
  std::string body_json;
  std::string rar_sha256;
  std::string prev_rar_sha256;
  std::string op_log_head_sha256;
  int64_t generated_at_unix{0};
  std::string signature;
};

struct RarChainVerifyReport {
  uint64_t entry_count{0};
  uint64_t last_sequence{0};
  bool consistent{true};
  std::string last_rar_sha256;
  std::vector<std::string> errors;
};

Status AppendRarChainEntry(const std::string& chain_path,
                           const RarChainEntry& entry);

Status ReadRarChainEntries(const std::string& chain_path,
                           std::vector<RarChainEntry>* out);

Status ReadLastRarChainEntry(const std::string& chain_path,
                             RarChainEntry* out, bool* found);

Status VerifyRarChain(const std::string& chain_path,
                      RarChainVerifyReport* out);

std::string RarChainLastSha256(const std::string& chain_path);

uint64_t RarChainNextSequence(const std::string& chain_path);

}  // namespace audit
}  // namespace ebtree
