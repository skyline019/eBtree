#include "ebtree/concept/btree/paged_btree.h"

#include <algorithm>
#include <cstring>
#include <fstream>
#include <unordered_map>

#include "ebtree/common/crc32.h"

namespace ebtree {

namespace {

constexpr size_t kInternalMaxEntries = 96;
constexpr size_t kTrieSummaryThreshold = 8;
constexpr uint8_t kLeafPrefixFormat = 2;
constexpr size_t kMinLeafPrefixLen = 4;

size_t CommonPrefixLen(const std::string& a, const std::string& b) {
  const size_t n = std::min(a.size(), b.size());
  size_t i = 0;
  while (i < n && a[i] == b[i]) ++i;
  return i;
}

size_t ChunkCommonPrefixFromKeys(
    const std::vector<std::pair<std::string, uint64_t>>& keys) {
  if (keys.size() < 2) return 0;
  std::string prefix = keys[0].first;
  for (size_t i = 1; i < keys.size(); ++i) {
    prefix = prefix.substr(0, CommonPrefixLen(prefix, keys[i].first));
    if (prefix.empty()) break;
  }
  return prefix.size();
}

struct LeafPageCursor {
  PageHeader hdr{};
  std::string shared_prefix;
  size_t pos{0};
  uint16_t index{0};
  const uint8_t* page_data{nullptr};
};

bool InitLeafCursor(const uint8_t* page_data, LeafPageCursor* cur) {
  if (!page_data || !cur) return false;
  cur->page_data = page_data;
  std::memcpy(&cur->hdr, page_data, sizeof(PageHeader));
  if (cur->hdr.type != kPageTypeLeaf) return false;
  cur->pos = sizeof(PageHeader);
  cur->index = 0;
  cur->shared_prefix.clear();
  if (cur->hdr.reserved == kLeafPrefixFormat) {
    if (cur->pos + sizeof(uint16_t) > kPageSize) return false;
    uint16_t prefix_len = 0;
    std::memcpy(&prefix_len, page_data + cur->pos, sizeof(prefix_len));
    cur->pos += sizeof(prefix_len);
    if (cur->pos + prefix_len > kPageSize) return false;
    cur->shared_prefix.assign(prefix_len, '\0');
    if (prefix_len) {
      std::memcpy(cur->shared_prefix.data(), page_data + cur->pos, prefix_len);
    }
    cur->pos += prefix_len;
  }
  return true;
}

bool NextLeafEntry(LeafPageCursor* cur, std::string* key, uint64_t* lsn) {
  if (!cur || !key || !lsn || cur->index >= cur->hdr.key_count) return false;
  if (cur->pos + sizeof(uint16_t) > kPageSize) return false;
  uint16_t klen = 0;
  std::memcpy(&klen, cur->page_data + cur->pos, sizeof(klen));
  cur->pos += sizeof(klen);
  if (cur->pos + klen + sizeof(uint64_t) > kPageSize) return false;
  if (cur->hdr.reserved == kLeafPrefixFormat) {
    key->assign(cur->shared_prefix);
    if (klen) {
      key->append(reinterpret_cast<const char*>(cur->page_data + cur->pos), klen);
    }
    cur->pos += klen;
  } else {
    key->assign(klen, '\0');
    if (klen) std::memcpy(key->data(), cur->page_data + cur->pos, klen);
    cur->pos += klen;
  }
  std::memcpy(lsn, cur->page_data + cur->pos, sizeof(*lsn));
  cur->pos += sizeof(*lsn);
  ++cur->index;
  return true;
}

uint8_t HistogramBinForKey(const std::string& key) {
  uint32_t hash = 2166136261u;
  for (unsigned char c : key) {
    hash ^= c;
    hash *= 16777619u;
  }
  return static_cast<uint8_t>(hash % kHistogramBinCount);
}

void BuildHistogramBins(const std::map<std::string, uint64_t>& source,
                        uint8_t* bins) {
  std::memset(bins, 0, kHistogramBinCount);
  for (const auto& kv : source) {
    ++bins[HistogramBinForKey(kv.first)];
  }
}

void BuildHistogramBinsFromKeys(
    const std::vector<std::pair<std::string, uint64_t>>& keys, uint8_t* bins) {
  std::memset(bins, 0, kHistogramBinCount);
  for (const auto& kv : keys) {
    ++bins[HistogramBinForKey(kv.first)];
  }
}

bool PrefixMatchesPlanKey(const PageHeader& hdr, const std::string& key) {
  if (hdr.summary_type != kSummaryTypeTrie || key.empty()) return true;
  const size_t prefix_len =
      std::min<size_t>(key.size(), kPageSummaryKeyBytes);
  if (prefix_len == 0) return true;
  return std::memcmp(hdr.min_key, key.data(), prefix_len) == 0;
}

#if defined(_MSC_VER) && defined(EBTREE_SIMD)
#include <intrin.h>
bool TriePrefixMatchSimd(const char* prefix, const char* key, size_t len) {
  if (len == 0) return true;
  if (len >= 16) {
    const __m128i a = _mm_loadu_si128(reinterpret_cast<const __m128i*>(prefix));
    const __m128i b = _mm_loadu_si128(reinterpret_cast<const __m128i*>(key));
    const __m128i eq = _mm_cmpeq_epi8(a, b);
    return _mm_movemask_epi8(eq) == 0xFFFF;
  }
  return std::memcmp(prefix, key, len) == 0;
}
#endif

bool HistogramCoversKey(const PageHeader& hdr, const std::string& key) {
  if (hdr.summary_type != kSummaryTypeHistogram || key.empty()) return true;
  const uint8_t bin = HistogramBinForKey(key);
  return hdr.min_key[bin] > 0;
}

bool HistogramCoversRange(const PageHeader& hdr, const std::string& range_start,
                          const std::string& range_end) {
  (void)hdr;
  (void)range_start;
  (void)range_end;
  return true;
}

void HistogramStringBounds(const PageHeader& hdr, std::string* min_k,
                           std::string* max_k) {
  if (min_k) min_k->clear();
  if (max_k) max_k->clear();
  if (hdr.summary_type != kSummaryTypeHistogram) return;
  const size_t min_len = hdr.reserved;
  const size_t min_copy = std::min(min_len, static_cast<size_t>(6));
  if (min_k && min_copy > 0) {
    min_k->assign(hdr.min_key + 8, min_copy);
  }
  const size_t max_copy =
      std::min<size_t>(hdr.max_key_len, kPageSummaryKeyBytes);
  if (max_k && max_copy > 0) {
    max_k->assign(hdr.max_key, max_copy);
  }
}

bool PageSummaryCoversKey(const PageHeader& hdr, const std::string& key) {
  if (key.empty()) return true;
  if (hdr.type == kPageTypeInternal) return true;
  std::string min_k;
  std::string max_k;
  if (hdr.summary_type == kSummaryTypeHistogram) {
    HistogramStringBounds(hdr, &min_k, &max_k);
  } else {
    min_k.assign(hdr.min_key,
                 std::min<size_t>(hdr.min_key_len, kPageSummaryKeyBytes));
    max_k.assign(hdr.max_key,
                 std::min<size_t>(hdr.max_key_len, kPageSummaryKeyBytes));
  }
  if (!min_k.empty() && key < min_k) return false;
  if (!max_k.empty() && key > max_k) return false;
  return true;
}

}  // namespace

PagedBTree::PagedBTree(PageFile* page_file) : page_file_(page_file) {}

void PagedBTree::SetPreferHistogramSummary(bool prefer) {
  prefer_histogram_summary_ = prefer;
}

void PagedBTree::TouchPage() const { ++pages_touched_; }

bool PagedBTree::RangeOverlapsSummary(const PageHeader& hdr,
                                      const std::string& range_start,
                                      const std::string& range_end) const {
  if (hdr.type == kPageTypeInternal) return true;
  std::string min_k;
  std::string max_k;
  if (hdr.summary_type == kSummaryTypeHistogram) {
    HistogramStringBounds(hdr, &min_k, &max_k);
  } else {
    min_k.assign(hdr.min_key,
                 std::min<size_t>(hdr.min_key_len, kPageSummaryKeyBytes));
    max_k.assign(hdr.max_key,
                 std::min<size_t>(hdr.max_key_len, kPageSummaryKeyBytes));
  }
  if (!max_k.empty() && range_start > max_k) return false;
  if (!min_k.empty() && range_end < min_k) return false;
  return true;
}

void PagedBTree::UpdateSummaryOnPut(const std::string& key) {
  if (index_.empty() && delta_deleted_.empty() && on_disk_mode_) {
    if (summary_.min_key.empty() || key < summary_.min_key) {
      summary_.min_key = key;
    }
    if (summary_.max_key.empty() || key > summary_.max_key) {
      summary_.max_key = key;
    }
  } else if (index_.size() == 1 && delta_deleted_.empty()) {
    summary_.min_key = key;
    summary_.max_key = key;
  } else {
    if (summary_.min_key.empty() || key < summary_.min_key) {
      summary_.min_key = key;
    }
    if (summary_.max_key.empty() || key > summary_.max_key) {
      summary_.max_key = key;
    }
  }
  summary_.summary_lsn = max_lsn_;
}

void PagedBTree::UpdateSummaryOnDelete(const std::string& key) {
  if (index_.empty() && delta_deleted_.empty() && on_disk_mode_) {
    summary_.summary_lsn = max_lsn_;
    return;
  }
  if (index_.empty() && delta_deleted_.empty()) {
    summary_ = {};
    summary_.summary_lsn = max_lsn_;
    return;
  }
  if (key != summary_.min_key && key != summary_.max_key) {
    summary_.summary_lsn = max_lsn_;
    return;
  }
  RebuildSummary();
}

Status PagedBTree::Put(const std::string& key, uint64_t data_lsn) {
  delta_deleted_.erase(key);
  index_[key] = data_lsn;
  max_lsn_ = std::max(max_lsn_, data_lsn);
  UpdateSummaryOnPut(key);
  return Status::Ok();
}

Status PagedBTree::DeleteKey(const std::string& key, uint64_t data_lsn) {
  index_.erase(key);
  delta_deleted_.insert(key);
  max_lsn_ = std::max(max_lsn_, data_lsn);
  UpdateSummaryOnDelete(key);
  return Status::Ok();
}

Status PagedBTree::Get(const std::string& key, uint64_t* data_lsn) const {
  if (delta_deleted_.count(key)) {
    return Status::NotFound("key deleted in delta");
  }
  const auto it = index_.find(key);
  if (it != index_.end()) {
    if (data_lsn) *data_lsn = it->second;
    return Status::Ok();
  }
  if (on_disk_mode_ && root_offset_ >= kPageFileHeaderSize) {
    return GetFromDisk(key, data_lsn);
  }
  return Status::NotFound("key not in btree");
}

Status PagedBTree::GetFromLeafChain(uint64_t offset, const std::string& key,
                                    uint64_t* data_lsn) const {
  while (offset >= kPageFileHeaderSize) {
    std::vector<uint8_t> page;
    const Status rs = ReadPageBytes(offset, &page);
    if (!rs.ok()) return rs;
    PageHeader hdr{};
    std::memcpy(&hdr, page.data(), sizeof(hdr));
    if (hdr.type != kPageTypeLeaf) {
      return Status::CorruptPage("expected leaf");
    }
    LeafPageCursor cur{};
    if (!InitLeafCursor(page.data(), &cur)) {
      return Status::CorruptPage("invalid leaf page");
    }
    std::string entry_key;
    uint64_t lsn = 0;
    while (NextLeafEntry(&cur, &entry_key, &lsn)) {
      if (entry_key == key) {
        if (data_lsn) *data_lsn = lsn;
        return Status::Ok();
      }
    }
    if (hdr.next_page_offset == 0 || hdr.next_page_offset < kPageFileHeaderSize) {
      break;
    }
    offset = hdr.next_page_offset;
  }
  return Status::NotFound("key not on disk");
}

Status PagedBTree::GetFromDiskAt(uint64_t offset, const std::string& key,
                                 uint64_t* data_lsn) const {
  PageHeader hdr{};
  const Status hs = ReadPageHeader(offset, &hdr);
  if (!hs.ok()) return hs;
  if (!PageSummaryCoversKey(hdr, key)) {
    return Status::NotFound("key pruned by summary");
  }
  if (hdr.type == kPageTypeLeaf) {
    return GetFromLeafChain(offset, key, data_lsn);
  }
  if (hdr.type != kPageTypeInternal) {
    return Status::CorruptPage("unknown page type");
  }
  std::vector<uint8_t> page;
  const Status rs = ReadPageBytes(offset, &page);
  if (!rs.ok()) return rs;
  size_t pos = sizeof(PageHeader);
  uint32_t child_off = 0;
  for (uint16_t i = 0; i < hdr.key_count; ++i) {
    if (pos + sizeof(uint32_t) + sizeof(uint16_t) > kPageSize) {
      return Status::CorruptPage("truncated internal page");
    }
    uint32_t off = 0;
    std::memcpy(&off, page.data() + pos, sizeof(off));
    pos += sizeof(off);
    uint16_t klen = 0;
    std::memcpy(&klen, page.data() + pos, sizeof(klen));
    pos += sizeof(klen);
    if (pos + klen > kPageSize) return Status::CorruptPage("truncated sep key");
    std::string sep(klen, '\0');
    if (klen) std::memcpy(sep.data(), page.data() + pos, klen);
    pos += klen;
    if (off < kPageFileHeaderSize) continue;
    if (sep > key) break;
    child_off = off;
  }
  if (child_off < kPageFileHeaderSize) {
    return Status::NotFound("no child for key");
  }
  return GetFromDiskAt(child_off, key, data_lsn);
}

Status PagedBTree::GetFromDisk(const std::string& key,
                               uint64_t* data_lsn) const {
  return GetFromDiskAt(root_offset_, key, data_lsn);
}

void PagedBTree::ScanLeafChain(
    uint64_t offset, const TypedPlan& plan,
    std::vector<std::pair<std::string, uint64_t>>* out) const {
  std::vector<uint64_t> chain;
  uint64_t cur = offset;
  while (cur >= kPageFileHeaderSize) {
    chain.push_back(cur);
    PageHeader hdr{};
    const Status hs = ReadPageHeader(cur, &hdr);
    if (!hs.ok()) return;
    if (hdr.type != kPageTypeLeaf) return;
    if (hdr.next_page_offset == 0 ||
        hdr.next_page_offset < kPageFileHeaderSize) {
      break;
    }
    cur = hdr.next_page_offset;
  }
  if (chain.empty()) return;

  constexpr size_t kReadWindow = 64;
  std::vector<std::vector<uint8_t>> pages;
  pages.reserve(chain.size());
  for (size_t start = 0; start < chain.size(); start += kReadWindow) {
    const size_t end = std::min(start + kReadWindow, chain.size());
    std::vector<uint64_t> window(chain.begin() + static_cast<std::ptrdiff_t>(start),
                                 chain.begin() + static_cast<std::ptrdiff_t>(end));
    std::vector<std::vector<uint8_t>> window_pages;
    if (page_file_) {
      const Status ws = page_file_->ReadPages(window, &window_pages);
      if (!ws.ok()) return;
    } else {
      window_pages.reserve(window.size());
      for (uint64_t off : window) {
        std::vector<uint8_t> page;
        if (!ReadPageBytes(off, &page).ok()) return;
        window_pages.push_back(std::move(page));
      }
    }
    pages.insert(pages.end(),
                 std::make_move_iterator(window_pages.begin()),
                 std::make_move_iterator(window_pages.end()));
  }

  for (const auto& page : pages) {
    if (page.size() < sizeof(PageHeader)) continue;
    LeafPageCursor leaf_cur{};
    if (!InitLeafCursor(page.data(), &leaf_cur)) continue;
    std::string entry_key;
    uint64_t lsn = 0;
    while (NextLeafEntry(&leaf_cur, &entry_key, &lsn)) {
      if (plan.op == PredicateOp::kEq) {
        if (entry_key == plan.key) out->emplace_back(entry_key, lsn);
      } else if (plan.op == PredicateOp::kRange) {
        if (entry_key >= plan.key && entry_key <= plan.range_end) {
          out->emplace_back(entry_key, lsn);
        }
      }
    }
  }
}

Status PagedBTree::ScanFromDiskAt(
    uint64_t offset, const TypedPlan& plan,
    std::vector<std::pair<std::string, uint64_t>>* out) const {
  PageHeader hdr{};
  const Status hs = ReadPageHeader(offset, &hdr);
  if (!hs.ok()) return hs;
  if (plan.op == PredicateOp::kEq) {
    if (!PageSummaryCoversKey(hdr, plan.key)) return Status::Ok();
  } else if (plan.op == PredicateOp::kRange) {
    if (!RangeOverlapsSummary(hdr, plan.key, plan.range_end)) {
      return Status::Ok();
    }
  }
  if (hdr.type == kPageTypeLeaf) {
    ScanLeafChain(offset, plan, out);
    return Status::Ok();
  }
  if (hdr.type != kPageTypeInternal) {
    return Status::CorruptPage("unknown page type");
  }
  std::vector<uint8_t> page;
  const Status rs = ReadPageBytes(offset, &page);
  if (!rs.ok()) return rs;
  std::vector<uint64_t> child_offs;
  child_offs.reserve(hdr.key_count);
  size_t pos = sizeof(PageHeader);
  for (uint16_t i = 0; i < hdr.key_count; ++i) {
    if (pos + sizeof(uint32_t) + sizeof(uint16_t) > kPageSize) {
      return Status::CorruptPage("truncated internal page");
    }
    uint32_t child_off = 0;
    std::memcpy(&child_off, page.data() + pos, sizeof(child_off));
    pos += sizeof(child_off);
    uint16_t klen = 0;
    std::memcpy(&klen, page.data() + pos, sizeof(klen));
    pos += sizeof(klen);
    if (pos + klen > kPageSize) return Status::CorruptPage("truncated sep key");
    pos += klen;
    if (child_off >= kPageFileHeaderSize) {
      child_offs.push_back(child_off);
    }
  }
  if (page_file_ && !child_offs.empty()) {
    std::vector<std::vector<uint8_t>> warmed;
    const Status ws = page_file_->ReadPages(child_offs, &warmed);
    if (!ws.ok()) return ws;
  }
  pos = sizeof(PageHeader);
  for (uint16_t i = 0; i < hdr.key_count; ++i) {
    if (pos + sizeof(uint32_t) + sizeof(uint16_t) > kPageSize) {
      return Status::CorruptPage("truncated internal page");
    }
    uint32_t child_off = 0;
    std::memcpy(&child_off, page.data() + pos, sizeof(child_off));
    pos += sizeof(child_off);
    uint16_t klen = 0;
    std::memcpy(&klen, page.data() + pos, sizeof(klen));
    pos += sizeof(klen);
    if (pos + klen > kPageSize) return Status::CorruptPage("truncated sep key");
    pos += klen;
    if (child_off < kPageFileHeaderSize) continue;
    PageHeader child_hdr{};
    const Status chs = ReadPageHeader(child_off, &child_hdr);
    if (!chs.ok()) return chs;
    if (plan.op == PredicateOp::kEq) {
      if (!PageSummaryCoversKey(child_hdr, plan.key)) continue;
    } else if (plan.op == PredicateOp::kRange) {
      if (!RangeOverlapsSummary(child_hdr, plan.key, plan.range_end)) continue;
    }
    const Status ss = ScanFromDiskAt(child_off, plan, out);
    if (!ss.ok()) return ss;
  }
  return Status::Ok();
}

Status PagedBTree::ScanFromDisk(
    const TypedPlan& plan,
    std::vector<std::pair<std::string, uint64_t>>* out) const {
  if (root_offset_ < kPageFileHeaderSize) return Status::Ok();
  return ScanFromDiskAt(root_offset_, plan, out);
}

void PagedBTree::MergeDeltaIntoScan(
    std::vector<std::pair<std::string, uint64_t>>* out) const {
  if (!out) return;
  std::unordered_map<std::string, uint64_t> merged;
  merged.reserve(out->size() + index_.size());
  for (const auto& hit : *out) merged[hit.first] = hit.second;
  for (const auto& kv : index_) merged[kv.first] = kv.second;
  for (const auto& del : delta_deleted_) merged.erase(del);
  out->clear();
  out->reserve(merged.size());
  for (const auto& kv : merged) out->emplace_back(kv.first, kv.second);
  std::sort(out->begin(), out->end(),
            [](const auto& a, const auto& b) { return a.first < b.first; });
}

Status PagedBTree::Scan(const TypedPlan& plan,
                        std::vector<std::pair<std::string, uint64_t>>* out) const {
  if (!out) return Status::InvalidArgument("out is null");
  out->clear();

  if (plan.snapshot_lsn > 0 && summary_.summary_lsn < plan.snapshot_lsn) {
    return Status::StaleSummary("summary lsn behind snapshot");
  }

  if (plan.op == PredicateOp::kEq) {
    if (!summary_.min_key.empty() && plan.key < summary_.min_key) {
      return Status::Ok();
    }
    if (!summary_.max_key.empty() && plan.key > summary_.max_key) {
      return Status::Ok();
    }
    uint64_t lsn = 0;
    const Status st = Get(plan.key, &lsn);
    if (st.ok()) out->emplace_back(plan.key, lsn);
    return Status::Ok();
  }

  if (plan.op == PredicateOp::kRange) {
    if (!summary_.max_key.empty() && plan.key > summary_.max_key) {
      return Status::Ok();
    }
    if (!summary_.min_key.empty() && plan.range_end < summary_.min_key) {
      return Status::Ok();
    }
    if (on_disk_mode_ && root_offset_ >= kPageFileHeaderSize) {
      const Status ds = ScanFromDisk(plan, out);
      if (!ds.ok()) return ds;
      MergeDeltaIntoScan(out);
      auto it = std::lower_bound(
          out->begin(), out->end(), plan.key,
          [](const auto& kv, const std::string& k) { return kv.first < k; });
      out->erase(out->begin(), it);
      while (!out->empty() && out->back().first > plan.range_end) {
        out->pop_back();
      }
      return Status::Ok();
    }
    auto it = index_.lower_bound(plan.key);
    while (it != index_.end() && it->first <= plan.range_end) {
      if (!delta_deleted_.count(it->first)) {
        out->emplace_back(it->first, it->second);
      }
      ++it;
    }
    return Status::Ok();
  }

  return Status::InvalidArgument("unsupported plan op");
}

void PagedBTree::LoadFromMap(const std::map<std::string, uint64_t>& data) {
  index_ = data;
  delta_deleted_.clear();
  max_lsn_ = 0;
  for (const auto& kv : index_) {
    max_lsn_ = std::max(max_lsn_, kv.second);
  }
  RebuildSummary();
  loaded_root_header_ = {};
  on_disk_mode_ = false;
}

void PagedBTree::RebuildSummary() {
  if (index_.empty()) {
    if (!on_disk_mode_) {
      summary_ = {};
      summary_.summary_lsn = max_lsn_;
    }
    return;
  }
  summary_.min_key = index_.begin()->first;
  summary_.max_key = index_.rbegin()->first;
  summary_.summary_lsn = max_lsn_;
}

void PagedBTree::RebuildSummaryFromIndex() { RebuildSummary(); }

void PagedBTree::RebuildSummaryFromCommitted(
    const std::map<std::string, uint64_t>& committed_lsn) {
  if (committed_lsn.empty()) return;
  summary_.min_key = committed_lsn.begin()->first;
  summary_.max_key = committed_lsn.rbegin()->first;
  for (const auto& kv : committed_lsn) {
    max_lsn_ = std::max(max_lsn_, kv.second);
  }
  summary_.summary_lsn = max_lsn_;
}

void PagedBTree::EnsureSummaryLsnAtLeast(uint64_t lsn) {
  summary_.summary_lsn = std::max(summary_.summary_lsn, lsn);
}

bool PagedBTree::SummaryDrifted() const {
  return max_lsn_ > 0 && summary_.summary_lsn < max_lsn_;
}

Status PagedBTree::ReadPageBytes(uint64_t offset,
                                 std::vector<uint8_t>* page) const {
  if (!page) return Status::InvalidArgument("page is null");
  page->assign(kPageSize, 0);
  if (!page_file_) return Status::InvalidArgument("no page file");
  const Status rs = page_file_->ReadPage(offset, page->data(), page->size());
  if (!rs.ok()) return rs;
  TouchPage();
  return Status::Ok();
}

Status PagedBTree::ReadPageHeader(uint64_t offset, PageHeader* hdr) const {
  if (!page_file_ || !hdr) return Status::InvalidArgument("bad read");
  std::vector<uint8_t> page;
  const Status rs = ReadPageBytes(offset, &page);
  if (!rs.ok()) return rs;
  std::memcpy(hdr, page.data(), sizeof(PageHeader));
  const uint32_t expected = Crc32(hdr, offsetof(PageHeader, page_crc));
  if (hdr->page_crc != expected) return Status::CorruptPage("page crc mismatch");
  return Status::Ok();
}

Status PagedBTree::SerializeLeafChunk(
    const std::map<std::string, uint64_t>& source,
    std::map<std::string, uint64_t>::const_iterator begin,
    std::map<std::string, uint64_t>::const_iterator end,
    std::vector<uint8_t>* out, std::string* first_key_out,
    std::map<std::string, uint64_t>::const_iterator* end_out) const {
  if (!out || !end_out || !first_key_out) return Status::InvalidArgument("null out");
  out->assign(kPageSize, 0);
  PageHeader hdr{};
  hdr.type = kPageTypeLeaf;
  hdr.summary_type =
      source.size() >= kTrieSummaryThreshold ? kSummaryTypeTrie : kSummaryTypeMinMax;
  hdr.max_lsn = max_lsn_;
  hdr.summary_lsn = summary_.summary_lsn;
  if (!source.empty()) {
    const std::string& min_k = source.begin()->first;
    const std::string& max_k = source.rbegin()->first;
    hdr.min_key_len = static_cast<uint16_t>(min_k.size());
    hdr.max_key_len = static_cast<uint16_t>(max_k.size());
    const size_t min_copy = std::min<size_t>(min_k.size(), kPageSummaryKeyBytes);
    const size_t max_copy = std::min<size_t>(max_k.size(), kPageSummaryKeyBytes);
    std::memcpy(hdr.min_key, min_k.data(), min_copy);
    std::memcpy(hdr.max_key, max_k.data(), max_copy);
  }

  size_t pos = sizeof(PageHeader);
  std::vector<std::pair<std::string, uint64_t>> chunk;
  for (auto fit = begin; fit != end; ++fit) {
    size_t used = sizeof(PageHeader);
    for (const auto& e : chunk) {
      used += sizeof(uint16_t) + e.first.size() + sizeof(uint64_t);
    }
    used += sizeof(uint16_t) + fit->first.size() + sizeof(uint64_t);
    if (used > kPageSize) break;
    chunk.push_back(*fit);
  }
  const size_t prefix_len = ChunkCommonPrefixFromKeys(chunk);
  const bool use_prefix =
      prefix_len >= kMinLeafPrefixLen && chunk.size() >= 2;
  pos = sizeof(PageHeader);
  uint16_t count = 0;
  bool first_set = false;
  if (use_prefix) {
    hdr.reserved = kLeafPrefixFormat;
    const std::string& prefix = begin->first.substr(0, prefix_len);
    const uint16_t plen = static_cast<uint16_t>(prefix.size());
    std::memcpy(out->data() + pos, &plen, sizeof(plen));
    pos += sizeof(plen);
    std::memcpy(out->data() + pos, prefix.data(), prefix.size());
    pos += prefix.size();
    for (const auto& entry : chunk) {
      const std::string suffix = entry.first.substr(prefix.size());
      const uint16_t slen = static_cast<uint16_t>(suffix.size());
      if (pos + sizeof(slen) + suffix.size() + sizeof(uint64_t) > kPageSize) {
        break;
      }
      if (!first_set) {
        *first_key_out = entry.first;
        first_set = true;
      }
      std::memcpy(out->data() + pos, &slen, sizeof(slen));
      pos += sizeof(slen);
      if (!suffix.empty()) {
        std::memcpy(out->data() + pos, suffix.data(), suffix.size());
        pos += suffix.size();
      }
      std::memcpy(out->data() + pos, &entry.second, sizeof(entry.second));
      pos += sizeof(entry.second);
      ++count;
    }
  } else {
    for (const auto& entry : chunk) {
      if (pos + sizeof(uint16_t) + entry.first.size() + sizeof(uint64_t) >
          kPageSize) {
        break;
      }
      if (!first_set) {
        *first_key_out = entry.first;
        first_set = true;
      }
      const uint16_t klen = static_cast<uint16_t>(entry.first.size());
      std::memcpy(out->data() + pos, &klen, sizeof(klen));
      pos += sizeof(klen);
      std::memcpy(out->data() + pos, entry.first.data(), klen);
      pos += klen;
      std::memcpy(out->data() + pos, &entry.second, sizeof(entry.second));
      pos += sizeof(entry.second);
      ++count;
    }
  }
  hdr.key_count = count;
  hdr.page_crc = Crc32(&hdr, offsetof(PageHeader, page_crc));
  std::memcpy(out->data(), &hdr, sizeof(hdr));
  auto end_it = begin;
  for (uint16_t i = 0; i < count && end_it != end; ++i) ++end_it;
  *end_out = end_it;
  return Status::Ok();
}

Status PagedBTree::SerializeInternalPage(
    const std::vector<std::pair<std::string, uint64_t>>& children,
    std::vector<uint8_t>* out,
    const std::map<std::string, uint64_t>* source) const {
  if (!out) return Status::InvalidArgument("out is null");
  out->assign(kPageSize, 0);
  PageHeader hdr{};
  hdr.type = kPageTypeInternal;
  if (prefer_histogram_summary_ &&
      children.size() >= kTrieSummaryThreshold) {
    hdr.summary_type = kSummaryTypeHistogram;
    BuildHistogramBinsFromKeys(children, reinterpret_cast<uint8_t*>(hdr.min_key));
    hdr.min_key_len = kHistogramBinCount;
  } else {
    hdr.summary_type =
        children.size() >= kTrieSummaryThreshold ? kSummaryTypeTrie
                                                 : kSummaryTypeMinMax;
  }
  hdr.max_lsn = max_lsn_;
  hdr.summary_lsn = summary_.summary_lsn;
  if (!children.empty()) {
    const std::string& min_k = children.front().first;
    const std::string& max_k = children.back().first;
    if (hdr.summary_type != kSummaryTypeHistogram) {
      hdr.min_key_len = static_cast<uint16_t>(min_k.size());
      hdr.max_key_len = static_cast<uint16_t>(max_k.size());
      const size_t min_copy = std::min<size_t>(min_k.size(), kPageSummaryKeyBytes);
      const size_t max_copy = std::min<size_t>(max_k.size(), kPageSummaryKeyBytes);
      std::memcpy(hdr.min_key, min_k.data(), min_copy);
      std::memcpy(hdr.max_key, max_k.data(), max_copy);
    } else {
      if (source) {
        BuildHistogramBins(*source, reinterpret_cast<uint8_t*>(hdr.min_key));
      }
      hdr.reserved = static_cast<uint8_t>(std::min<size_t>(min_k.size(), 6));
      hdr.max_key_len = static_cast<uint16_t>(
          std::min<size_t>(max_k.size(), kPageSummaryKeyBytes));
      const size_t min_copy = hdr.reserved;
      if (min_copy > 0) {
        std::memcpy(hdr.min_key + 8, min_k.data(), min_copy);
      }
      const size_t max_copy =
          std::min<size_t>(max_k.size(), kPageSummaryKeyBytes);
      std::memcpy(hdr.max_key, max_k.data(), max_copy);
    }
  }

  size_t pos = sizeof(PageHeader);
  uint16_t count = 0;
  for (const auto& child : children) {
    const size_t need =
        sizeof(uint32_t) + sizeof(uint16_t) + child.first.size();
    if (pos + need > kPageSize) break;
    const uint32_t off = static_cast<uint32_t>(child.second);
    std::memcpy(out->data() + pos, &off, sizeof(off));
    pos += sizeof(off);
    const uint16_t klen = static_cast<uint16_t>(child.first.size());
    std::memcpy(out->data() + pos, &klen, sizeof(klen));
    pos += sizeof(klen);
    std::memcpy(out->data() + pos, child.first.data(), klen);
    pos += klen;
    ++count;
  }
  hdr.key_count = count;
  hdr.page_crc = Crc32(&hdr, offsetof(PageHeader, page_crc));
  std::memcpy(out->data(), &hdr, sizeof(hdr));
  return Status::Ok();
}

Status PagedBTree::DeserializeLeafPage(const uint8_t* page_data,
                                       uint32_t* next_page_out,
                                       bool clear_index) {
  if (!page_data) return Status::InvalidArgument("null page");
  if (clear_index) {
    index_.clear();
    delta_deleted_.clear();
    summary_ = {};
    max_lsn_ = 0;
  }
  if (next_page_out) *next_page_out = 0;
  PageHeader hdr{};
  std::memcpy(&hdr, page_data, sizeof(hdr));
  if (hdr.type != kPageTypeLeaf) {
    return Status::CorruptPage("not a leaf page");
  }
  const uint32_t expected = Crc32(&hdr, offsetof(PageHeader, page_crc));
  if (hdr.page_crc != expected) {
    return Status::CorruptPage("leaf page crc mismatch");
  }
  max_lsn_ = std::max(max_lsn_, hdr.max_lsn);
  LeafPageCursor cur{};
  if (!InitLeafCursor(page_data, &cur)) {
    return Status::CorruptPage("invalid leaf page");
  }
  std::string key;
  uint64_t lsn = 0;
  while (NextLeafEntry(&cur, &key, &lsn)) {
    index_[key] = lsn;
  }
  summary_.min_key.assign(
      hdr.min_key, std::min<size_t>(hdr.min_key_len, kPageSummaryKeyBytes));
  summary_.max_key.assign(
      hdr.max_key, std::min<size_t>(hdr.max_key_len, kPageSummaryKeyBytes));
  summary_.summary_lsn = std::max(summary_.summary_lsn, hdr.summary_lsn);
  if (next_page_out) *next_page_out = hdr.next_page_offset;
  return Status::Ok();
}

Status PagedBTree::DeserializeLeafChain(uint64_t offset, bool clear_index) {
  if (clear_index) {
    index_.clear();
    delta_deleted_.clear();
    summary_ = {};
    max_lsn_ = 0;
  }
  while (offset >= kPageFileHeaderSize) {
    std::vector<uint8_t> page;
    const Status rs = ReadPageBytes(offset, &page);
    if (!rs.ok()) return rs;
    uint32_t next = 0;
    const Status ds = DeserializeLeafPage(page.data(), &next, false);
    if (!ds.ok()) return ds;
    if (next == 0 || next < kPageFileHeaderSize) break;
    offset = next;
  }
  RebuildSummary();
  return Status::Ok();
}

Status PagedBTree::LoadTreeRecursive(uint64_t offset) {
  PageHeader hdr{};
  const Status hs = ReadPageHeader(offset, &hdr);
  if (!hs.ok()) return hs;
  loaded_root_header_ = hdr;
  if (hdr.type == kPageTypeLeaf) {
    return DeserializeLeafChain(offset, true);
  }
  if (hdr.type != kPageTypeInternal) {
    return Status::CorruptPage("unknown page type");
  }
  index_.clear();
  delta_deleted_.clear();
  summary_ = {};
  max_lsn_ = 0;
  std::vector<uint8_t> page;
  const Status rs = ReadPageBytes(offset, &page);
  if (!rs.ok()) return rs;
  size_t pos = sizeof(PageHeader);
  for (uint16_t i = 0; i < hdr.key_count; ++i) {
    if (pos + sizeof(uint32_t) + sizeof(uint16_t) > kPageSize) {
      return Status::CorruptPage("truncated internal page");
    }
    uint32_t child_off = 0;
    std::memcpy(&child_off, page.data() + pos, sizeof(child_off));
    pos += sizeof(child_off);
    uint16_t klen = 0;
    std::memcpy(&klen, page.data() + pos, sizeof(klen));
    pos += sizeof(klen);
    if (pos + klen > kPageSize) return Status::CorruptPage("truncated sep key");
    pos += klen;
    if (child_off < kPageFileHeaderSize) continue;
    PageHeader child_hdr{};
    const Status chs = ReadPageHeader(child_off, &child_hdr);
    if (!chs.ok()) return chs;
    if (child_hdr.type == kPageTypeLeaf) {
      const Status ls = DeserializeLeafChain(child_off, false);
      if (!ls.ok()) return ls;
    } else {
      const Status ls = LoadTreeRecursive(child_off);
      if (!ls.ok()) return ls;
    }
  }
  RebuildSummary();
  on_disk_mode_ = false;
  return Status::Ok();
}

Status PagedBTree::BuildInternalRoot(
    const std::vector<std::pair<std::string, uint64_t>>& children,
    uint64_t* root_offset_out,
    const std::map<std::string, uint64_t>* source) {
  if (children.size() <= 1) {
    if (children.empty()) return Status::Internal("no children");
    if (root_offset_out) *root_offset_out = children.front().second;
    return Status::Ok();
  }
  if (children.size() <= kInternalMaxEntries) {
    std::vector<uint8_t> page;
    const Status ss = SerializeInternalPage(children, &page, source);
    if (!ss.ok()) return ss;
    uint64_t off = 0;
    const Status ap = page_file_->AppendPages({page.data()}, &off);
    if (!ap.ok()) return ap;
    if (root_offset_out) *root_offset_out = off;
    return Status::Ok();
  }
  std::vector<std::pair<std::string, uint64_t>> level;
  for (size_t i = 0; i < children.size(); i += kInternalMaxEntries) {
    const size_t end =
        std::min(children.size(), i + kInternalMaxEntries);
    std::vector<std::pair<std::string, uint64_t>> group(children.begin() + i,
                                                          children.begin() + end);
    std::vector<uint8_t> page;
    const Status ss = SerializeInternalPage(group, &page, source);
    if (!ss.ok()) return ss;
    uint64_t off = 0;
    const Status ap = page_file_->AppendPages({page.data()}, &off);
    if (!ap.ok()) return ap;
    level.emplace_back(group.front().first, off);
  }
  return BuildInternalRoot(level, root_offset_out, source);
}

Status PagedBTree::PersistRootImpl(const std::map<std::string, uint64_t>& source,
                                   uint64_t* root_offset_out) {
  if (!page_file_) return Status::Internal("no page file");
  if (source.empty()) {
    root_offset_ = kLegacyMapRoot;
    if (root_offset_out) *root_offset_out = root_offset_;
    return Status::Ok();
  }
  std::vector<std::vector<uint8_t>> pages;
  std::vector<std::pair<std::string, uint64_t>> leaf_refs;
  auto it = source.begin();
  while (it != source.end()) {
    std::vector<uint8_t> page;
    std::string first_key;
    const Status ss =
        SerializeLeafChunk(source, it, source.end(), &page, &first_key, &it);
    if (!ss.ok()) return ss;
    pages.push_back(std::move(page));
    leaf_refs.emplace_back(std::move(first_key), 0);
  }
  uint64_t base = page_file_->append_offset();
  for (size_t i = 0; i < pages.size(); ++i) {
    leaf_refs[i].second = base + static_cast<uint64_t>(i) * kPageSize;
    if (i + 1 < pages.size()) {
      const uint32_t next_off =
          static_cast<uint32_t>(base + static_cast<uint64_t>(i + 1) * kPageSize);
      PageHeader hdr{};
      std::memcpy(&hdr, pages[i].data(), sizeof(hdr));
      hdr.next_page_offset = next_off;
      hdr.page_crc = Crc32(&hdr, offsetof(PageHeader, page_crc));
      std::memcpy(pages[i].data(), &hdr, sizeof(hdr));
    }
  }
  std::vector<const uint8_t*> page_ptrs;
  page_ptrs.reserve(pages.size());
  for (const auto& page : pages) page_ptrs.push_back(page.data());
  uint64_t first = 0;
  const Status ap = page_file_->AppendPages(page_ptrs, &first);
  if (!ap.ok()) return ap;
  uint64_t root = first;
  if (pages.size() > 1) {
    for (size_t i = 0; i < leaf_refs.size(); ++i) {
      leaf_refs[i].second = first + static_cast<uint64_t>(i) * kPageSize;
    }
    const Status br = BuildInternalRoot(leaf_refs, &root, &source);
    if (!br.ok()) return br;
  }
  root_offset_ = root;
  index_.clear();
  delta_deleted_.clear();
  on_disk_mode_ = true;
  if (root_offset_out) *root_offset_out = root;
  return Status::Ok();
}

Status PagedBTree::PersistRoot(uint64_t* root_offset_out) {
  std::map<std::string, uint64_t> merged = index_;
  return PersistRootImpl(merged, root_offset_out);
}

Status PagedBTree::PersistRootFromMap(const std::map<std::string, uint64_t>& full_map,
                                      uint64_t* root_offset_out) {
  return PersistRootImpl(full_map, root_offset_out);
}

Status PagedBTree::LoadRoot(uint64_t root_offset) {
  if (!page_file_) return Status::Internal("no page file");
  if (root_offset < kPageFileHeaderSize) {
    return Status::InvalidArgument("invalid root offset");
  }
  pages_touched_ = 0;
  index_.clear();
  delta_deleted_.clear();
  PageHeader hdr{};
  const Status hs = ReadPageHeader(root_offset, &hdr);
  if (!hs.ok()) return hs;
  loaded_root_header_ = hdr;
  if (hdr.summary_type == kSummaryTypeHistogram) {
    HistogramStringBounds(hdr, &summary_.min_key, &summary_.max_key);
  } else {
    summary_.min_key.assign(
        hdr.min_key, std::min<size_t>(hdr.min_key_len, kPageSummaryKeyBytes));
    summary_.max_key.assign(
        hdr.max_key, std::min<size_t>(hdr.max_key_len, kPageSummaryKeyBytes));
  }
  summary_.summary_lsn = hdr.summary_lsn;
  max_lsn_ = hdr.max_lsn;
  root_offset_ = root_offset;
  on_disk_mode_ = true;
  return Status::Ok();
}

Status PagedBTree::LoadTreeForHeal(uint64_t root_offset) {
  on_disk_mode_ = false;
  pages_touched_ = 0;
  const Status st = LoadTreeRecursive(root_offset);
  if (!st.ok()) return st;
  root_offset_ = root_offset;
  return Status::Ok();
}

Status PagedBTree::CorruptRootPageForTest() {
  if (!page_file_ || root_offset_ < kPageFileHeaderSize) {
    return Status::Internal("no persisted root");
  }
  std::fstream file(page_file_->path(),
                    std::ios::binary | std::ios::in | std::ios::out);
  if (!file) return Status::IoError("pagefile open failed");
  file.seekg(static_cast<std::streamoff>(root_offset_));
  PageHeader hdr{};
  file.read(reinterpret_cast<char*>(&hdr), sizeof(hdr));
  if (!file) return Status::IoError("read root failed");
  hdr.page_crc ^= 0xFFFFFFFFu;
  file.seekp(static_cast<std::streamoff>(root_offset_));
  file.write(reinterpret_cast<const char*>(&hdr.page_crc), sizeof(hdr.page_crc));
  if (!file) return Status::IoError("corrupt write failed");
  return Status::Ok();
}

}  // namespace ebtree
