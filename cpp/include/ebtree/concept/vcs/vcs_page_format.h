#pragma once

#include <cstdint>

#include "ebtree/concept/page/page_format.h"

namespace ebtree {

constexpr uint8_t kPageTypeVcsChain = 0x04;
constexpr size_t kVcsNodeRecordSize = 16;
constexpr size_t kVcsNodesPerPage =
    (kPageSize - sizeof(PageHeader)) / kVcsNodeRecordSize;

#pragma pack(push, 1)
struct VcsNodeRecord {
  uint64_t lsn{0};
  uint64_t prev_lsn{0};
};
#pragma pack(pop)

static_assert(sizeof(VcsNodeRecord) == kVcsNodeRecordSize,
              "VcsNodeRecord must be 16 bytes");

}  // namespace ebtree
