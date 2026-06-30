#pragma once

#include <cstdint>

namespace ebtree {

constexpr size_t kPageSize = 4096;
constexpr size_t kPageFileHeaderSize = 4096;
constexpr uint64_t kLegacyMapRoot = 1;
constexpr uint8_t kPageTypeInternal = 0x01;
constexpr uint8_t kPageTypeLeaf = 0x02;
constexpr uint8_t kSummaryTypeMinMax = 0;
constexpr uint8_t kSummaryTypeTrie = 1;
constexpr uint8_t kSummaryTypeHistogram = 2;
constexpr size_t kHistogramBinCount = 8;
constexpr size_t kPageSummaryKeyBytes = 14;

#pragma pack(push, 1)
struct PageHeader {
  uint8_t type{0};
  uint16_t key_count{0};
  uint8_t summary_type{kSummaryTypeMinMax};
  uint8_t reserved{0};
  uint64_t max_lsn{0};
  uint64_t summary_lsn{0};
  uint16_t min_key_len{0};
  uint16_t max_key_len{0};
  char min_key[kPageSummaryKeyBytes]{};
  char max_key[kPageSummaryKeyBytes]{};
  uint32_t next_page_offset{0};
  uint32_t page_crc{0};
  uint8_t padding[3]{};
};
#pragma pack(pop)

static_assert(sizeof(PageHeader) == 64, "PageHeader must be 64 bytes");

struct LeafEntry {
  uint16_t key_len{0};
  uint64_t lsn{0};
};

}  // namespace ebtree
