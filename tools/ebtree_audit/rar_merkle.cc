#include "rar_merkle.h"

#include "digest.h"
#include "rar_chain.h"

#include <cstdlib>
#include <fstream>
#include <sstream>

namespace ebtree {
namespace audit {

std::string CarlMerkleHashLeaf(const std::string& rar_sha256) {
  return Sha256HexString(std::string("carl:leaf:") + rar_sha256);
}

std::string CarlMerkleHashNode(const std::string& left, const std::string& right) {
  return Sha256HexString(std::string("carl:node:") + left + right);
}

namespace {

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

CarlMerkleBatch ParseBatchLine(const std::string& line) {
  CarlMerkleBatch batch{};
  (void)ExtractJsonUint64Field(line, "batch_id", &batch.batch_id);
  (void)ExtractJsonUint64Field(line, "start_sequence", &batch.start_sequence);
  (void)ExtractJsonUint64Field(line, "end_sequence", &batch.end_sequence);
  (void)ExtractJsonUint64Field(line, "leaf_count", &batch.leaf_count);
  (void)ExtractJsonStringField(line, "root_hash", &batch.root_hash);
  return batch;
}

std::string BatchToJsonLine(const CarlMerkleBatch& batch) {
  std::ostringstream oss;
  oss << "{\"batch_id\":" << batch.batch_id << ",\"start_sequence\":"
      << batch.start_sequence << ",\"end_sequence\":" << batch.end_sequence
      << ",\"leaf_count\":" << batch.leaf_count << ",\"root_hash\":\""
      << batch.root_hash << "\"}";
  return oss.str();
}

std::string HashCarlNode(const std::string& left, const std::string& right) {
  return CarlMerkleHashNode(left, right);
}

std::string MerkleRootFromLeaves(const std::vector<std::string>& leaves) {
  if (leaves.empty()) return {};
  std::vector<std::string> level = leaves;
  while (level.size() > 1) {
    std::vector<std::string> next;
    for (size_t i = 0; i < level.size(); i += 2) {
      if (i + 1 < level.size()) {
        next.push_back(HashCarlNode(level[i], level[i + 1]));
      } else {
        next.push_back(HashCarlNode(level[i], level[i]));
      }
    }
    level.swap(next);
  }
  return level.front();
}

}  // namespace

CarlMerkleAccumulator::CarlMerkleAccumulator(uint64_t batch_size)
    : batch_size_(batch_size > 0 ? batch_size : 8) {}

std::string CarlMerkleAccumulator::MerkleRoot(
    const std::vector<std::string>& leaves) {
  return MerkleRootFromLeaves(leaves);
}

void CarlMerkleAccumulator::AppendLeaf(uint64_t sequence,
                                       const std::string& rar_sha256) {
  if (pending_leaves_.empty()) {
    batch_start_sequence_ = sequence;
  }
  pending_sequences_.push_back(sequence);
  pending_leaves_.push_back(CarlMerkleHashLeaf(rar_sha256));
}

bool CarlMerkleAccumulator::ShouldFlush() const {
  return pending_leaves_.size() >= batch_size_;
}

CarlMerkleBatch CarlMerkleAccumulator::FlushBatch() {
  CarlMerkleBatch batch{};
  if (pending_leaves_.empty()) return batch;
  batch.batch_id = next_batch_id_++;
  batch.start_sequence = batch_start_sequence_;
  batch.end_sequence = pending_sequences_.back();
  batch.leaf_count = pending_leaves_.size();
  batch.root_hash = MerkleRoot(pending_leaves_);
  pending_leaves_.clear();
  pending_sequences_.clear();
  batch_start_sequence_ = 0;
  return batch;
}

std::string CarlMerkleAccumulator::CurrentRoot() const {
  return MerkleRoot(pending_leaves_);
}

std::string CarlMerkleSidecarPath(const std::string& chain_path) {
  return chain_path + ".merkle.jsonl";
}

Status PersistCarlMerkleBatch(const std::string& merkle_path,
                              const CarlMerkleBatch& batch) {
  std::ofstream out(merkle_path, std::ios::app);
  if (!out) return Status::IoError("merkle open failed: " + merkle_path);
  out << BatchToJsonLine(batch) << "\n";
  if (!out) return Status::IoError("merkle append failed");
  return Status::Ok();
}

Status LoadCarlMerkleBatches(const std::string& merkle_path,
                             std::vector<CarlMerkleBatch>* out) {
  if (!out) return Status::InvalidArgument("out is null");
  out->clear();
  std::ifstream in(merkle_path);
  if (!in) return Status::Ok();
  std::string line;
  while (std::getline(in, line)) {
    if (line.empty()) continue;
    out->push_back(ParseBatchLine(line));
  }
  return Status::Ok();
}

Status GenerateCarlMerkleProof(const std::string& merkle_path, uint64_t sequence,
                               std::vector<std::string>* proof,
                               std::string* root_hash) {
  if (!proof || !root_hash) return Status::InvalidArgument("null out");
  proof->clear();
  root_hash->clear();

  if (merkle_path.size() <= 13 ||
      merkle_path.rfind(".merkle.jsonl") != merkle_path.size() - 13) {
    return Status::InvalidArgument("invalid merkle sidecar path");
  }
  const std::string chain_path =
      merkle_path.substr(0, merkle_path.size() - 13);

  std::vector<RarChainEntry> entries;
  const Status rs = ReadRarChainEntries(chain_path, &entries);
  if (!rs.ok()) return rs;

  std::vector<CarlMerkleBatch> batches;
  const Status bs = LoadCarlMerkleBatches(merkle_path, &batches);
  if (!bs.ok()) return bs;
  if (batches.empty()) return Status::NotFound("no merkle batches");

  const CarlMerkleBatch* batch = nullptr;
  for (const auto& b : batches) {
    if (sequence >= b.start_sequence && sequence <= b.end_sequence) {
      batch = &b;
      break;
    }
  }
  if (!batch) return Status::NotFound("sequence not in any batch");

  CarlMerkleAccumulator acc(1);
  std::vector<std::string> leaves;
  size_t target_index = 0;
  bool found = false;
  for (const auto& entry : entries) {
    if (entry.sequence < batch->start_sequence ||
        entry.sequence > batch->end_sequence) {
      continue;
    }
    if (entry.sequence == sequence) {
      target_index = leaves.size();
      found = true;
    }
    leaves.push_back(CarlMerkleHashLeaf(entry.rar_sha256));
  }
  if (!found || leaves.empty()) {
    return Status::NotFound("sequence not found in batch leaves");
  }

  std::vector<std::string> level = leaves;
  size_t pos = target_index;
  while (level.size() > 1) {
    const size_t sibling = (pos % 2 == 0) ? pos + 1 : pos - 1;
    if (sibling < level.size()) {
      proof->push_back((pos % 2 == 0 ? "R:" : "L:") + level[sibling]);
    } else {
      proof->push_back((pos % 2 == 0 ? "R:" : "L:") + level[pos]);
    }
    std::vector<std::string> next;
    for (size_t i = 0; i < level.size(); i += 2) {
      if (i + 1 < level.size()) {
        next.push_back(HashCarlNode(level[i], level[i + 1]));
      } else {
        next.push_back(HashCarlNode(level[i], level[i]));
      }
    }
    pos /= 2;
    level.swap(next);
  }
  *root_hash = batch->root_hash;
  return Status::Ok();
}

Status VerifyCarlMerkleInclusion(const std::string& leaf_hash,
                                 const std::vector<std::string>& proof,
                                 const std::string& root_hash) {
  std::string current = leaf_hash;
  for (const auto& step : proof) {
    if (step.size() < 3 || (step[0] != 'L' && step[0] != 'R') || step[1] != ':') {
      return Status::InvalidArgument("invalid merkle proof step");
    }
    const std::string sibling = step.substr(2);
    if (step[0] == 'L') {
      current = CarlMerkleHashNode(sibling, current);
    } else {
      current = CarlMerkleHashNode(current, sibling);
    }
  }
  if (current != root_hash) {
    return Status::Corrupt("merkle inclusion proof failed");
  }
  return Status::Ok();
}

}  // namespace audit
}  // namespace ebtree
