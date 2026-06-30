#pragma once

#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "ebtree/common/status.h"
#include "ebtree/concept/page/page_file.h"

namespace ebtree {

class PagedBTree;

enum class PredicateOp { kEq, kRange };

struct TypedPlan {
  PredicateOp op{PredicateOp::kEq};
  std::string key;
  std::string range_end;
  uint64_t snapshot_lsn{0};
};

#pragma pack(push, 1)
struct BTreeSummary {
  std::string min_key;
  std::string max_key;
  uint64_t summary_lsn{0};
};
#pragma pack(pop)

class BTreeIndex {
 public:
  BTreeIndex();
  explicit BTreeIndex(std::string page_file_path);
  ~BTreeIndex();

  void InitPages(const std::string& page_file_path);

  Status Put(const std::string& key, uint64_t data_lsn);
  Status DeleteKey(const std::string& key, uint64_t data_lsn);
  Status Get(const std::string& key, uint64_t* data_lsn) const;
  Status Scan(const TypedPlan& plan,
              std::vector<std::pair<std::string, uint64_t>>* out) const;

  uint64_t max_lsn() const;
  const BTreeSummary& summary() const;
  void SetSummaryLsnForTest(uint64_t lsn);

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
  uint64_t root_offset() const;
  bool on_disk_mode() const;

  Status CorruptRootPageForTest();

  uint64_t pages_touched() const;

  PageFile* page_file() { return page_file_.get(); }
  void SetPreferHistogramSummary(bool prefer);

 private:
  std::unique_ptr<PageFile> page_file_;
  std::unique_ptr<PagedBTree> paged_;
};

}  // namespace ebtree
