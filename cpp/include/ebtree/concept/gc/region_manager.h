#pragma once

#include <cstdint>
#include <fstream>
#include <string>

#include "ebtree/common/status.h"

namespace ebtree {

#pragma pack(push, 1)
struct GcMetaCritical {
  uint64_t magic{0xEB710CULL};
  uint64_t epoch{0};
  uint8_t active_generation{0};
  uint8_t reserved[7]{};
  uint32_t crc32{0};
};
#pragma pack(pop)

struct GcMeta {
  GcMetaCritical critical{};
  uint8_t padding[4068]{};
};

static_assert(sizeof(GcMeta) == 4096, "GcMeta must be 4096 bytes");

class RegionManager {
 public:
  explicit RegionManager(std::string path);

  uint8_t active_generation() const { return active_generation_; }
  uint8_t reclaim_generation() const { return reclaim_generation_; }
  Status Load();
  Status SwapRegions();
  Status MaybeSwap(uint64_t datafile_bytes, uint64_t threshold_bytes);

  const std::string& path() const { return path_; }

 private:
  Status ReadSlot(int slot, GcMeta* out);
  Status WriteSlot(int slot, const GcMeta& block);
  void FinalizeCritical(GcMeta* block);
  bool ValidateCritical(const GcMetaCritical& c) const;

  std::string path_;
  uint8_t active_generation_{0};
  uint8_t reclaim_generation_{0xFF};
  uint64_t epoch_{0};
};

}  // namespace ebtree
