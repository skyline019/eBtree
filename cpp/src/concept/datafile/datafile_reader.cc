#include "ebtree/concept/datafile/datafile_reader.h"

namespace ebtree {

DataFileReader::DataFileReader(DataFile* datafile, MmapWindowManager* mmap_mgr)
    : datafile_(datafile), mmap_mgr_(mmap_mgr) {}

Status DataFileReader::ReadByLsn(uint64_t lsn, std::string* value,
                                 uint8_t reclaim_gen) {
  if (!datafile_ || !value) return Status::InvalidArgument("invalid read");
  uint64_t offset = 0;
  if (!datafile_->lsn_index().Lookup(lsn, &offset)) {
    return Status::NotFound("lsn not in datafile index");
  }
  std::string key;
  bool deleted = false;
  uint64_t rec_lsn = 0;
  const Status rs = datafile_->ReadRecordAt(offset, &key, value, &rec_lsn,
                                            &deleted, reclaim_gen);
  if (!rs.ok()) return rs;
  if (deleted) return Status::NotFound("deleted at lsn");
  return Status::Ok();
}

Status DataFileReader::ReadBatch(
    const std::vector<std::pair<uint64_t, size_t>>& offset_pairs,
    std::vector<std::string>* values, uint8_t reclaim_gen) {
  if (!datafile_ || !values) return Status::InvalidArgument("invalid batch read");
  return datafile_->ReadRecordsAtOffsets(mmap_mgr_, offset_pairs, values,
                                         reclaim_gen);
}

}  // namespace ebtree
