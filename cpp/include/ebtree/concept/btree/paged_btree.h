#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <unordered_set>
#include <vector>

#include "ebtree/common/status.h"
#include "ebtree/concept/btree/btree.h"
#include "ebtree/concept/page/page_file.h"
#include "ebtree/concept/page/page_format.h"

namespace ebtree {

class PagedBTree {
 public:
  explicit PagedBTree(PageFile* page_file);

  Status Put(const std::string& key, uint64_t data_lsn);
  Status DeleteKey(const std::string& key, uint64_t data_lsn);
  Status Get(const std::string& key, uint64_t* data_lsn) const;
  Status Scan(const TypedPlan& plan,
              std::vector<std::pair<std::string, uint64_t>>* out) const;

  uint64_t max_lsn() const { return max_lsn_; }
  const BTreeSummary& summary() const { return summary_; }
  uint64_t pages_touched() const { return pages_touched_; }
  void SetSummaryLsnForTest(uint64_t lsn) { summary_.summary_lsn = lsn; }
  uint64_t root_offset() const { return root_offset_; }
  bool on_disk_mode() const { return on_disk_mode_; }
  void SetPreferHistogramSummary(bool prefer);

  void LoadFromMap(const std::map<std::string, uint64_t>& data);
  void RebuildSummaryFromIndex();
  void RebuildSummaryFromCommitted(
      const std::map<std::string, uint64_t>& committed_lsn);
  void EnsureSummaryLsnAtLeast(uint64_t lsn);
  bool SummaryDrifted() const;

  Status PersistRoot(uint64_t* root_offset_out);
  Status PersistRootFromMap(const std::map<std::string, uint64_t>& full_map,
                            uint64_t* root_offset_out);
  Status LoadRoot(uint64_t root_offset);
  Status LoadTreeForHeal(uint64_t root_offset);

  Status CorruptRootPageForTest();

 private:
  void RebuildSummary();
  void UpdateSummaryOnPut(const std::string& key);
  void UpdateSummaryOnDelete(const std::string& key);
  void TouchPage() const;
  bool RangeOverlapsSummary(const PageHeader& hdr, const std::string& range_start,
                            const std::string& range_end) const;

  Status ReadPageHeader(uint64_t offset, PageHeader* hdr) const;
  Status ReadPageBytes(uint64_t offset, std::vector<uint8_t>* page) const;
  Status GetFromDisk(const std::string& key, uint64_t* data_lsn) const;
  Status GetFromDiskAt(uint64_t offset, const std::string& key,
                       uint64_t* data_lsn) const;
  Status GetFromLeafChain(uint64_t offset, const std::string& key,
                          uint64_t* data_lsn) const;
  Status ScanFromDisk(const TypedPlan& plan,
                      std::vector<std::pair<std::string, uint64_t>>* out) const;
  Status ScanFromDiskAt(uint64_t offset, const TypedPlan& plan,
                        std::vector<std::pair<std::string, uint64_t>>* out) const;
  void ScanLeafChain(uint64_t offset, const TypedPlan& plan,
                     std::vector<std::pair<std::string, uint64_t>>* out) const;
  void MergeDeltaIntoScan(std::vector<std::pair<std::string, uint64_t>>* out) const;

  Status SerializeLeafChunk(
      const std::map<std::string, uint64_t>& source,
      std::map<std::string, uint64_t>::const_iterator begin,
      std::map<std::string, uint64_t>::const_iterator end,
      std::vector<uint8_t>* out, std::string* first_key_out,
      std::map<std::string, uint64_t>::const_iterator* end_out) const;
  Status SerializeInternalPage(
      const std::vector<std::pair<std::string, uint64_t>>& children,
      std::vector<uint8_t>* out,
      const std::map<std::string, uint64_t>* source) const;
  Status DeserializeLeafPage(const uint8_t* page_data, uint32_t* next_page_out,
                             bool clear_index = false);
  Status DeserializeLeafChain(uint64_t offset, bool clear_index = true);
  Status LoadTreeRecursive(uint64_t offset);
  Status BuildInternalRoot(
      const std::vector<std::pair<std::string, uint64_t>>& children,
      uint64_t* root_offset_out,
      const std::map<std::string, uint64_t>* source);
  Status PersistRootImpl(const std::map<std::string, uint64_t>& source,
                         uint64_t* root_offset_out);

  PageFile* page_file_{nullptr};
  std::map<std::string, uint64_t> index_;
  std::unordered_set<std::string> delta_deleted_;
  BTreeSummary summary_{};
  PageHeader loaded_root_header_{};
  uint64_t max_lsn_{0};
  uint64_t root_offset_{0};
  mutable uint64_t pages_touched_{0};
  bool on_disk_mode_{false};
  bool prefer_histogram_summary_{false};
};

}  // namespace ebtree
