#include "ebtree/concept/btree/btree.h"

#include "ebtree/concept/btree/paged_btree.h"

namespace ebtree {

BTreeIndex::BTreeIndex() : paged_(std::make_unique<PagedBTree>(nullptr)) {}

BTreeIndex::~BTreeIndex() = default;

BTreeIndex::BTreeIndex(std::string page_file_path)
    : page_file_(std::make_unique<PageFile>(std::move(page_file_path))),
      paged_(std::make_unique<PagedBTree>(page_file_.get())) {}

void BTreeIndex::InitPages(const std::string& page_file_path) {
  page_file_ = std::make_unique<PageFile>(page_file_path);
  paged_ = std::make_unique<PagedBTree>(page_file_.get());
}

Status BTreeIndex::Put(const std::string& key, uint64_t data_lsn) {
  return paged_->Put(key, data_lsn);
}

Status BTreeIndex::DeleteKey(const std::string& key, uint64_t data_lsn) {
  return paged_->DeleteKey(key, data_lsn);
}

Status BTreeIndex::Get(const std::string& key, uint64_t* data_lsn) const {
  return paged_->Get(key, data_lsn);
}

Status BTreeIndex::Scan(const TypedPlan& plan,
                        std::vector<std::pair<std::string, uint64_t>>* out) const {
  return paged_->Scan(plan, out);
}

uint64_t BTreeIndex::max_lsn() const { return paged_->max_lsn(); }

const BTreeSummary& BTreeIndex::summary() const { return paged_->summary(); }

void BTreeIndex::SetSummaryLsnForTest(uint64_t lsn) {
  paged_->SetSummaryLsnForTest(lsn);
}

void BTreeIndex::LoadFromMap(const std::map<std::string, uint64_t>& data) {
  paged_->LoadFromMap(data);
}

void BTreeIndex::RebuildSummaryFromIndex() {
  paged_->RebuildSummaryFromIndex();
}

void BTreeIndex::RebuildSummaryFromCommitted(
    const std::map<std::string, uint64_t>& committed_lsn) {
  paged_->RebuildSummaryFromCommitted(committed_lsn);
}

void BTreeIndex::EnsureSummaryLsnAtLeast(uint64_t lsn) {
  paged_->EnsureSummaryLsnAtLeast(lsn);
}

bool BTreeIndex::SummaryDrifted() const {
  return paged_->SummaryDrifted();
}

Status BTreeIndex::PersistRoot(uint64_t* root_offset_out) {
  return paged_->PersistRoot(root_offset_out);
}

Status BTreeIndex::PersistRootFromMap(
    const std::map<std::string, uint64_t>& full_map, uint64_t* root_offset_out) {
  return paged_->PersistRootFromMap(full_map, root_offset_out);
}

Status BTreeIndex::LoadRoot(uint64_t root_offset) {
  return paged_->LoadRoot(root_offset);
}

uint64_t BTreeIndex::root_offset() const { return paged_->root_offset(); }

bool BTreeIndex::on_disk_mode() const { return paged_->on_disk_mode(); }

void BTreeIndex::SetPreferHistogramSummary(bool prefer) {
  paged_->SetPreferHistogramSummary(prefer);
}

Status BTreeIndex::CorruptRootPageForTest() {
  return paged_->CorruptRootPageForTest();
}

uint64_t BTreeIndex::pages_touched() const { return paged_->pages_touched(); }

}  // namespace ebtree
