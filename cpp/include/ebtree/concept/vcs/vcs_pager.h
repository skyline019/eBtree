#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "ebtree/common/status.h"
#include "ebtree/concept/vcs/version_chain_store.h"

namespace ebtree {

class PageFile;

struct VcsKeyOverflowMeta {
  uint64_t head_page{0};
  uint64_t tail_page{0};
  uint32_t node_count{0};
};

class VcsPager {
 public:
  explicit VcsPager(std::string path);
  ~VcsPager();

  Status Open();
  Status Sync();

  Status AppendOverflow(const std::string& key, const VersionNode& node);
  Status LoadOverflow(const std::string& key,
                      std::vector<VersionNode>* nodes) const;
  uint32_t OverflowCount(const std::string& key) const;

  Status SaveMeta(const std::string& path) const;
  Status LoadMeta(const std::string& path);

  void Clear();
  const std::string& path() const { return path_; }

 private:
  Status AppendNodeToChain(VcsKeyOverflowMeta* meta, const VersionNode& node);
  Status ReadPageNodes(uint64_t offset, std::vector<VersionNode>* nodes) const;

  std::string path_;
  std::unique_ptr<PageFile> pages_;
  std::unordered_map<std::string, VcsKeyOverflowMeta> overflow_;
  std::unordered_map<std::string, std::vector<VersionNode>> overflow_nodes_;
};

}  // namespace ebtree
