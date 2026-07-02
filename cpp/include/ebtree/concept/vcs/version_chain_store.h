#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "ebtree/common/status.h"

namespace ebtree {

class DataFile;
class VcsPager;

struct VersionNode {
  uint64_t lsn{0};
  uint64_t prev_lsn{0};
};

constexpr size_t kVcsInlineMax = 8;

class VersionChainStore {
 public:
  VersionChainStore();
  ~VersionChainStore();

  Status OpenPager(const std::string& page_path, const std::string& meta_path);
  void ClosePager();

  Status Append(const std::string& key, uint64_t lsn, uint64_t prev_lsn);
  uint64_t Floor(const std::string& key, uint64_t snapshot_lsn) const;
  uint64_t Head(const std::string& key) const;
  bool ContainsLsn(const std::string& key, uint64_t lsn) const;
  std::vector<uint64_t> ReferencedLsnsAbove(uint64_t min_lsn) const;

  Status SaveToFile(const std::string& path) const;
  Status LoadFromFile(const std::string& path);
  Status RebuildFromDataFile(DataFile* datafile, uint8_t reclaim_generation);
  void CompactBelow(uint64_t lsn);
  void Clear();

  size_t KeyCount() const { return chains_.size(); }
  size_t InlineNodeCount(const std::string& key) const;
  uint32_t OverflowNodeCount(const std::string& key) const;

 private:
  struct KeyChain {
    std::vector<VersionNode> inline_nodes;
  };

  void MaybeSpill(const std::string& key);
  std::vector<VersionNode> MergedChain(const std::string& key) const;

  std::unordered_map<std::string, KeyChain> chains_;
  std::unique_ptr<VcsPager> pager_;
  std::string pager_meta_path_;
};

}  // namespace ebtree
