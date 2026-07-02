#include "ebtree/concept/vcs/version_chain_store.h"

#include <algorithm>
#include <fstream>

#include "ebtree/concept/vcs/vcs_pager.h"
#include "ebtree/concept/datafile/datafile.h"

namespace ebtree {

namespace {

constexpr uint64_t kVidxMagicV1 = 0xEB56494458ULL;
constexpr uint64_t kVidxMagicV2Flag = 0xEB5649445832ULL;

}  // namespace

VersionChainStore::VersionChainStore() = default;
VersionChainStore::~VersionChainStore() = default;

Status VersionChainStore::OpenPager(const std::string& page_path,
                                    const std::string& meta_path) {
  pager_ = std::make_unique<VcsPager>(page_path);
  pager_meta_path_ = meta_path;
  const Status st = pager_->Open();
  if (!st.ok()) return st;
  if (!meta_path.empty()) {
    const Status lm = pager_->LoadMeta(meta_path);
    if (!lm.ok() && lm.code() != StatusCode::kNotFound) return lm;
  }
  return Status::Ok();
}

void VersionChainStore::ClosePager() {
  pager_.reset();
  pager_meta_path_.clear();
}

void VersionChainStore::MaybeSpill(const std::string& key) {
  if (!pager_) return;
  auto& chain = chains_[key];
  while (chain.inline_nodes.size() > kVcsInlineMax) {
    const VersionNode node = chain.inline_nodes.front();
    chain.inline_nodes.erase(chain.inline_nodes.begin());
    const Status st = pager_->AppendOverflow(key, node);
    if (!st.ok()) break;
  }
}

std::vector<VersionNode> VersionChainStore::MergedChain(
    const std::string& key) const {
  std::vector<VersionNode> merged;
  if (pager_) {
    const Status st = pager_->LoadOverflow(key, &merged);
    if (!st.ok()) return {};
  }
  const auto it = chains_.find(key);
  if (it != chains_.end()) {
    merged.insert(merged.end(), it->second.inline_nodes.begin(),
                  it->second.inline_nodes.end());
  }
  return merged;
}

Status VersionChainStore::Append(const std::string& key, uint64_t lsn,
                                 uint64_t prev_lsn) {
  if (key.empty()) return Status::InvalidArgument("empty key");
  if (lsn == 0) return Status::InvalidArgument("lsn must be > 0");
  auto& chain = chains_[key];
  if (!chain.inline_nodes.empty() &&
      chain.inline_nodes.back().lsn >= lsn) {
    return Status::InvalidArgument("lsn must increase");
  }
  if (pager_ && pager_->OverflowCount(key) > 0) {
    std::vector<VersionNode> overflow;
    const Status lo = pager_->LoadOverflow(key, &overflow);
    if (lo.ok() && !overflow.empty() && overflow.back().lsn >= lsn) {
      return Status::InvalidArgument("lsn must increase");
    }
  }
  chain.inline_nodes.push_back(VersionNode{lsn, prev_lsn});
  MaybeSpill(key);
  return Status::Ok();
}

uint64_t VersionChainStore::Floor(const std::string& key,
                                  uint64_t snapshot_lsn) const {
  const std::vector<VersionNode> chain = MergedChain(key);
  if (chain.empty()) return 0;
  uint64_t best = 0;
  for (const VersionNode& node : chain) {
    if (node.lsn <= snapshot_lsn && node.lsn >= best) best = node.lsn;
  }
  return best;
}

uint64_t VersionChainStore::Head(const std::string& key) const {
  const auto it = chains_.find(key);
  if (it != chains_.end() && !it->second.inline_nodes.empty()) {
    return it->second.inline_nodes.back().lsn;
  }
  if (pager_) {
    std::vector<VersionNode> overflow;
    if (pager_->LoadOverflow(key, &overflow).ok() && !overflow.empty()) {
      return overflow.back().lsn;
    }
  }
  return 0;
}

bool VersionChainStore::ContainsLsn(const std::string& key,
                                    uint64_t lsn) const {
  const std::vector<VersionNode> chain = MergedChain(key);
  for (const VersionNode& node : chain) {
    if (node.lsn == lsn) return true;
  }
  return false;
}

std::vector<uint64_t> VersionChainStore::ReferencedLsnsAbove(
    uint64_t min_lsn) const {
  std::vector<uint64_t> out;
  for (const auto& kv : chains_) {
  const std::vector<VersionNode> chain = MergedChain(kv.first);
    for (const VersionNode& node : chain) {
      if (node.lsn > min_lsn) out.push_back(node.lsn);
    }
  }
  std::sort(out.begin(), out.end());
  out.erase(std::unique(out.begin(), out.end()), out.end());
  return out;
}

size_t VersionChainStore::InlineNodeCount(const std::string& key) const {
  const auto it = chains_.find(key);
  if (it == chains_.end()) return 0;
  return it->second.inline_nodes.size();
}

uint32_t VersionChainStore::OverflowNodeCount(const std::string& key) const {
  if (!pager_) return 0;
  return pager_->OverflowCount(key);
}

Status VersionChainStore::SaveToFile(const std::string& path) const {
  std::ofstream out(path, std::ios::binary | std::ios::trunc);
  if (!out) return Status::IoError("vidx create failed");

  const uint64_t magic =
      pager_ ? kVidxMagicV2Flag : kVidxMagicV1;
  out.write(reinterpret_cast<const char*>(&magic), sizeof(magic));

  const uint64_t key_count = chains_.size();
  out.write(reinterpret_cast<const char*>(&key_count), sizeof(key_count));

  for (const auto& kv : chains_) {
    const uint16_t klen = static_cast<uint16_t>(kv.first.size());
    out.write(reinterpret_cast<const char*>(&klen), sizeof(klen));
    if (klen) out.write(kv.first.data(), klen);

    const std::vector<VersionNode> merged = MergedChain(kv.first);
    const uint32_t node_count = static_cast<uint32_t>(merged.size());
    out.write(reinterpret_cast<const char*>(&node_count), sizeof(node_count));
    for (const VersionNode& node : merged) {
      out.write(reinterpret_cast<const char*>(&node.lsn), sizeof(node.lsn));
      out.write(reinterpret_cast<const char*>(&node.prev_lsn),
                sizeof(node.prev_lsn));
    }
  }
  if (!out) return Status::IoError("vidx write failed");

  if (pager_ && !pager_meta_path_.empty()) {
    const Status sm = pager_->SaveMeta(pager_meta_path_);
    if (!sm.ok()) return sm;
    const Status sy = pager_->Sync();
    if (!sy.ok()) return sy;
  }
  return Status::Ok();
}

Status VersionChainStore::LoadFromFile(const std::string& path) {
  std::ifstream in(path, std::ios::binary);
  if (!in) return Status::NotFound("vidx missing");

  uint64_t magic = 0;
  in.read(reinterpret_cast<char*>(&magic), sizeof(magic));
  if (!in || (magic != kVidxMagicV1 && magic != kVidxMagicV2Flag)) {
    return Status::CorruptPage("bad vidx magic");
  }

  chains_.clear();
  if (pager_) pager_->Clear();

  uint64_t key_count = 0;
  in.read(reinterpret_cast<char*>(&key_count), sizeof(key_count));
  if (!in) return Status::CorruptPage("truncated vidx header");

  for (uint64_t i = 0; i < key_count; ++i) {
    uint16_t klen = 0;
    in.read(reinterpret_cast<char*>(&klen), sizeof(klen));
    if (!in) return Status::CorruptPage("truncated vidx key");
    std::string key(klen, '\0');
    if (klen) in.read(key.data(), klen);

    uint32_t node_count = 0;
    in.read(reinterpret_cast<char*>(&node_count), sizeof(node_count));
    if (!in) return Status::CorruptPage("truncated vidx node count");

    std::vector<VersionNode> nodes;
    nodes.reserve(node_count);
    for (uint32_t n = 0; n < node_count; ++n) {
      VersionNode node{};
      in.read(reinterpret_cast<char*>(&node.lsn), sizeof(node.lsn));
      in.read(reinterpret_cast<char*>(&node.prev_lsn), sizeof(node.prev_lsn));
      if (!in) return Status::CorruptPage("truncated vidx node");
      nodes.push_back(node);
    }

    if (magic == kVidxMagicV2Flag && pager_ && nodes.size() > kVcsInlineMax) {
      const size_t spill = nodes.size() - kVcsInlineMax;
      for (size_t s = 0; s < spill; ++s) {
        const Status ap = pager_->AppendOverflow(key, nodes[s]);
        if (!ap.ok()) return ap;
      }
      KeyChain chain;
      chain.inline_nodes.assign(nodes.begin() + static_cast<ptrdiff_t>(spill),
                                nodes.end());
      chains_[key] = std::move(chain);
    } else {
      chains_[key] = KeyChain{std::move(nodes)};
    }
  }

  if (magic == kVidxMagicV2Flag && pager_ && !pager_meta_path_.empty()) {
    const Status lm = pager_->LoadMeta(pager_meta_path_);
    if (!lm.ok() && lm.code() != StatusCode::kNotFound) return lm;
  }
  return Status::Ok();
}

void VersionChainStore::Clear() {
  chains_.clear();
  if (pager_) pager_->Clear();
}

void VersionChainStore::CompactBelow(uint64_t lsn) {
  if (lsn == 0) return;
  std::unordered_map<std::string, std::vector<VersionNode>> snapshot;
  for (const auto& kv : chains_) {
    snapshot[kv.first] = MergedChain(kv.first);
  }
  Clear();
  for (const auto& kv : snapshot) {
    std::vector<VersionNode> kept;
    for (const VersionNode& node : kv.second) {
      if (node.lsn >= lsn) kept.push_back(node);
    }
    uint64_t floor_at_pin = 0;
    for (const VersionNode& node : kv.second) {
      if (node.lsn <= lsn && node.lsn >= floor_at_pin) floor_at_pin = node.lsn;
    }
    if (floor_at_pin > 0) {
      bool have_visible = false;
      for (const VersionNode& node : kept) {
        if (node.lsn <= lsn) have_visible = true;
      }
      if (!have_visible) {
        for (const VersionNode& node : kv.second) {
          if (node.lsn == floor_at_pin) {
            kept.push_back(node);
            break;
          }
        }
      }
    }
    if (kept.empty()) continue;
    std::sort(kept.begin(), kept.end(),
              [](const VersionNode& a, const VersionNode& b) {
                return a.lsn < b.lsn;
              });
    uint64_t prev = 0;
    for (const VersionNode& node : kept) {
      const Status st = Append(kv.first, node.lsn, prev);
      if (!st.ok()) break;
      prev = node.lsn;
    }
  }
}

Status VersionChainStore::RebuildFromDataFile(DataFile* datafile,
                                              uint8_t reclaim_generation) {
  if (!datafile) return Status::InvalidArgument("null datafile");
  Clear();
  std::ifstream in(datafile->path(), std::ios::binary);
  if (!in) return Status::Ok();

  struct Rec {
    std::string key;
    uint64_t lsn{0};
  };
  std::vector<Rec> recs;

  while (in) {
    DataRecordHeader hdr{};
    in.read(reinterpret_cast<char*>(&hdr), sizeof(hdr));
    if (!in) break;
    std::string key(hdr.key_len, '\0');
    std::string value(hdr.value_len, '\0');
    if (hdr.key_len) in.read(key.data(), hdr.key_len);
    if (hdr.value_len) in.read(value.data(), hdr.value_len);
    if (!in) break;
    if (reclaim_generation != 0xFF &&
        hdr.reserved[0] == reclaim_generation) {
      continue;
    }
    recs.push_back(Rec{std::move(key), hdr.lsn});
  }

  std::sort(recs.begin(), recs.end(),
            [](const Rec& a, const Rec& b) { return a.lsn < b.lsn; });
  for (const Rec& rec : recs) {
    const uint64_t prev = Head(rec.key);
    const Status st = Append(rec.key, rec.lsn, prev);
    if (!st.ok()) return st;
  }
  return Status::Ok();
}

}  // namespace ebtree
