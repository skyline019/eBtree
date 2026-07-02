#include "ebtree/concept/vcs/vcs_pager.h"

#include <algorithm>
#include <cstring>
#include <fstream>

#include "ebtree/concept/page/page_file.h"
#include "ebtree/concept/vcs/vcs_page_format.h"

namespace ebtree {

namespace {

constexpr uint64_t kVcsMetaMagic = 0xEB5643534D455441ULL;

struct VcsPagerMetaHeader {
  uint64_t magic{kVcsMetaMagic};
  uint64_t key_count{0};
};

Status WriteOverflowPage(PageFile* pages, const std::vector<VersionNode>& nodes,
                         uint64_t* offset_out) {
  if (!pages || nodes.empty() || !offset_out) {
    return Status::InvalidArgument("invalid overflow write");
  }
  uint8_t page_buf[kPageSize]{};
  auto* hdr = reinterpret_cast<PageHeader*>(page_buf);
  hdr->type = kPageTypeVcsChain;
  const uint16_t count = static_cast<uint16_t>(
      std::min<size_t>(nodes.size(), kVcsNodesPerPage));
  hdr->key_count = count;
  hdr->max_lsn = nodes[count - 1].lsn;
  auto* rec = reinterpret_cast<VcsNodeRecord*>(page_buf + sizeof(PageHeader));
  for (uint16_t i = 0; i < count; ++i) {
    rec[i].lsn = nodes[i].lsn;
    rec[i].prev_lsn = nodes[i].prev_lsn;
  }
  return pages->AppendPage(page_buf, kPageSize, offset_out);
}

}  // namespace

VcsPager::VcsPager(std::string path) : path_(std::move(path)) {}

VcsPager::~VcsPager() = default;

Status VcsPager::Open() {
  pages_ = std::make_unique<PageFile>(path_);
  return Status::Ok();
}

Status VcsPager::Sync() { return Status::Ok(); }

Status VcsPager::AppendNodeToChain(VcsKeyOverflowMeta* meta,
                                   const VersionNode& node) {
  (void)meta;
  return Status::Ok();
}

Status VcsPager::ReadPageNodes(uint64_t offset,
                               std::vector<VersionNode>* nodes) const {
  if (!nodes || !pages_) return Status::InvalidArgument("invalid read");
  nodes->clear();
  uint64_t cur = offset;
  while (cur != 0) {
    uint8_t page_buf[kPageSize]{};
    const Status rp = pages_->ReadPage(cur, page_buf, kPageSize);
    if (!rp.ok()) return rp;
    const auto* hdr = reinterpret_cast<const PageHeader*>(page_buf);
    if (hdr->type != kPageTypeVcsChain) {
      return Status::CorruptPage("bad vcs page type");
    }
    const auto* rec =
        reinterpret_cast<const VcsNodeRecord*>(page_buf + sizeof(PageHeader));
    for (uint16_t i = 0; i < hdr->key_count; ++i) {
      nodes->push_back(VersionNode{rec[i].lsn, rec[i].prev_lsn});
    }
    cur = hdr->next_page_offset;
  }
  return Status::Ok();
}

Status VcsPager::AppendOverflow(const std::string& key,
                                const VersionNode& node) {
  if (key.empty()) return Status::InvalidArgument("empty key");
  auto& nodes = overflow_nodes_[key];
  if (!nodes.empty() && nodes.back().lsn >= node.lsn) {
    return Status::InvalidArgument("lsn must increase");
  }
  nodes.push_back(node);
  auto& meta = overflow_[key];
  meta.node_count = static_cast<uint32_t>(nodes.size());
  return Status::Ok();
}

Status VcsPager::LoadOverflow(const std::string& key,
                              std::vector<VersionNode>* nodes) const {
  if (!nodes) return Status::InvalidArgument("nodes is null");
  nodes->clear();
  const auto it = overflow_nodes_.find(key);
  if (it != overflow_nodes_.end()) {
    *nodes = it->second;
    return Status::Ok();
  }
  const auto meta_it = overflow_.find(key);
  if (meta_it == overflow_.end() || meta_it->second.head_page == 0) {
    return Status::Ok();
  }
  return ReadPageNodes(meta_it->second.head_page, nodes);
}

uint32_t VcsPager::OverflowCount(const std::string& key) const {
  const auto it = overflow_nodes_.find(key);
  if (it != overflow_nodes_.end()) {
    return static_cast<uint32_t>(it->second.size());
  }
  const auto meta_it = overflow_.find(key);
  if (meta_it == overflow_.end()) return 0;
  return meta_it->second.node_count;
}

void VcsPager::Clear() {
  overflow_.clear();
  overflow_nodes_.clear();
  if (pages_) pages_ = std::make_unique<PageFile>(path_);
}

Status VcsPager::SaveMeta(const std::string& path) const {
  std::unordered_map<std::string, VcsKeyOverflowMeta> meta_out = overflow_;
  if (pages_) {
    for (const auto& kv : overflow_nodes_) {
      if (kv.second.empty()) continue;
      uint64_t head = 0;
      uint64_t tail = 0;
      for (size_t pos = 0; pos < kv.second.size(); pos += kVcsNodesPerPage) {
        const size_t end =
            std::min(pos + kVcsNodesPerPage, kv.second.size());
        std::vector<VersionNode> slice(kv.second.begin() +
                                       static_cast<ptrdiff_t>(pos),
                                       kv.second.begin() +
                                       static_cast<ptrdiff_t>(end));
        uint64_t offset = 0;
        const Status wp = WriteOverflowPage(pages_.get(), slice, &offset);
        if (!wp.ok()) return wp;
        if (head == 0) head = offset;
        if (tail != 0) {
          uint8_t old_tail[kPageSize]{};
          const Status rp = pages_->ReadPage(tail, old_tail, kPageSize);
          if (!rp.ok()) return rp;
          auto* hdr = reinterpret_cast<PageHeader*>(old_tail);
          hdr->next_page_offset = static_cast<uint32_t>(offset);
          std::fstream out(pages_->path(),
                           std::ios::binary | std::ios::in | std::ios::out);
          if (!out) return Status::IoError("vcs page link failed");
          out.seekp(static_cast<std::streamoff>(tail));
          out.write(reinterpret_cast<const char*>(old_tail), kPageSize);
          if (!out) return Status::IoError("vcs page link failed");
        }
        tail = offset;
      }
      VcsKeyOverflowMeta meta{};
      meta.head_page = head;
      meta.tail_page = tail;
      meta.node_count = static_cast<uint32_t>(kv.second.size());
      meta_out[kv.first] = meta;
    }
  }

  std::ofstream out(path, std::ios::binary | std::ios::trunc);
  if (!out) return Status::IoError("vcs meta create failed");
  VcsPagerMetaHeader hdr{};
  hdr.key_count = meta_out.size();
  out.write(reinterpret_cast<const char*>(&hdr), sizeof(hdr));
  for (const auto& kv : meta_out) {
    const uint16_t klen = static_cast<uint16_t>(kv.first.size());
    out.write(reinterpret_cast<const char*>(&klen), sizeof(klen));
    if (klen) out.write(kv.first.data(), klen);
    out.write(reinterpret_cast<const char*>(&kv.second), sizeof(kv.second));
    const auto nodes_it = overflow_nodes_.find(kv.first);
    const uint32_t node_count =
        nodes_it == overflow_nodes_.end()
            ? 0
            : static_cast<uint32_t>(nodes_it->second.size());
    out.write(reinterpret_cast<const char*>(&node_count), sizeof(node_count));
    if (nodes_it != overflow_nodes_.end()) {
      for (const VersionNode& node : nodes_it->second) {
        out.write(reinterpret_cast<const char*>(&node.lsn), sizeof(node.lsn));
        out.write(reinterpret_cast<const char*>(&node.prev_lsn),
                  sizeof(node.prev_lsn));
      }
    }
  }
  if (!out) return Status::IoError("vcs meta write failed");
  return Status::Ok();
}

Status VcsPager::LoadMeta(const std::string& path) {
  overflow_.clear();
  overflow_nodes_.clear();
  std::ifstream in(path, std::ios::binary);
  if (!in) return Status::NotFound("vcs meta missing");
  VcsPagerMetaHeader hdr{};
  in.read(reinterpret_cast<char*>(&hdr), sizeof(hdr));
  if (!in || hdr.magic != kVcsMetaMagic) {
    return Status::CorruptPage("bad vcs meta");
  }
  for (uint64_t i = 0; i < hdr.key_count; ++i) {
    uint16_t klen = 0;
    in.read(reinterpret_cast<char*>(&klen), sizeof(klen));
    if (!in) return Status::CorruptPage("truncated vcs meta key");
    std::string key(klen, '\0');
    if (klen) in.read(key.data(), klen);
    VcsKeyOverflowMeta meta{};
    in.read(reinterpret_cast<char*>(&meta), sizeof(meta));
    if (!in) return Status::CorruptPage("truncated vcs meta entry");
    overflow_[key] = meta;
    uint32_t node_count = 0;
    in.read(reinterpret_cast<char*>(&node_count), sizeof(node_count));
    if (!in) return Status::CorruptPage("truncated vcs meta nodes");
    std::vector<VersionNode> nodes;
    nodes.reserve(node_count);
    for (uint32_t n = 0; n < node_count; ++n) {
      VersionNode node{};
      in.read(reinterpret_cast<char*>(&node.lsn), sizeof(node.lsn));
      in.read(reinterpret_cast<char*>(&node.prev_lsn), sizeof(node.prev_lsn));
      if (!in) return Status::CorruptPage("truncated vcs meta node");
      nodes.push_back(node);
    }
    if (!nodes.empty()) overflow_nodes_[key] = std::move(nodes);
  }
  return Status::Ok();
}

}  // namespace ebtree
