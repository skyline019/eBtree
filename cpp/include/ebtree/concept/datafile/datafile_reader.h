#pragma once

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "ebtree/common/config.h"
#include "ebtree/common/status.h"
#include "ebtree/concept/datafile/datafile.h"
#include "ebtree/concept/mmap/mmap_window.h"

namespace ebtree {

class DataFileReader {
 public:
  DataFileReader() = default;
  DataFileReader(DataFile* datafile, MmapWindowManager* mmap_mgr);

  Status ReadByLsn(uint64_t lsn, std::string* value, uint8_t reclaim_gen);
  Status ReadBatch(const std::vector<std::pair<uint64_t, size_t>>& offset_pairs,
                   std::vector<std::string>* values, uint8_t reclaim_gen);

 private:
  DataFile* datafile_;
  MmapWindowManager* mmap_mgr_;
};

}  // namespace ebtree
