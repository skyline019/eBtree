#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "ebtree/common/status.h"

namespace ebtree {
namespace audit {

struct CarlMerkleBatch {
  uint64_t batch_id{0};
  uint64_t start_sequence{0};
  uint64_t end_sequence{0};
  std::string root_hash;
  uint64_t leaf_count{0};
};

class CarlMerkleAccumulator {
 public:
  explicit CarlMerkleAccumulator(uint64_t batch_size = 8);

  void AppendLeaf(uint64_t sequence, const std::string& rar_sha256);
  bool ShouldFlush() const;
  CarlMerkleBatch FlushBatch();
  std::string CurrentRoot() const;

 private:
  static std::string MerkleRoot(const std::vector<std::string>& leaves);

  uint64_t batch_size_;
  uint64_t next_batch_id_{1};
  uint64_t batch_start_sequence_{0};
  std::vector<std::string> pending_leaves_;
  std::vector<uint64_t> pending_sequences_;
};

std::string CarlMerkleSidecarPath(const std::string& chain_path);

std::string CarlMerkleHashLeaf(const std::string& rar_sha256);

std::string CarlMerkleHashNode(const std::string& left, const std::string& right);

Status PersistCarlMerkleBatch(const std::string& merkle_path,
                              const CarlMerkleBatch& batch);

Status LoadCarlMerkleBatches(const std::string& merkle_path,
                             std::vector<CarlMerkleBatch>* out);

Status GenerateCarlMerkleProof(const std::string& merkle_path, uint64_t sequence,
                               std::vector<std::string>* proof,
                               std::string* root_hash);

Status VerifyCarlMerkleInclusion(const std::string& leaf_hash,
                                 const std::vector<std::string>& proof,
                                 const std::string& root_hash);

}  // namespace audit
}  // namespace ebtree
