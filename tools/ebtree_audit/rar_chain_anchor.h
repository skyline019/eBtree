#pragma once

#include <cstdint>
#include <string>

#include "ebtree/common/status.h"

namespace ebtree {
namespace audit {

struct CarlSignedTreeHead {
  uint64_t chain_sequence{0};
  std::string root_hash;
  int64_t published_at_unix{0};
  std::string signature;
  std::string chain_path;
};

std::string DefaultCarlAnchorDir(const std::string& engine_path);

Status PublishCarlAnchor(const std::string& chain_path,
                         const std::string& anchor_dir,
                         CarlSignedTreeHead* out);

Status LoadLatestCarlAnchor(const std::string& anchor_dir,
                            const std::string& chain_basename,
                            CarlSignedTreeHead* out, bool* found);

Status VerifyCarlAnchorAgainstChain(const std::string& chain_path,
                                    const CarlSignedTreeHead& anchor,
                                    std::string* mismatch_reason);

Status VerifyCarlAnchorSignature(const CarlSignedTreeHead& anchor,
                                 const std::string& pubkey);

Status VerifyCarlAnchorRequired(const std::string& chain_path,
                                const std::string& anchor_dir,
                                bool require_signature = false);

void MaybeAutoPublishCarlAnchor(const std::string& chain_path);

}  // namespace audit
}  // namespace ebtree
