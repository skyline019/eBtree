#pragma once

#include <cstdint>
#include <fstream>
#include <string>

#include "ebtree/common/crc32.h"
#include "ebtree/common/status.h"

namespace ebtree {

constexpr uint64_t kSuperBlockMagic = 0xEBE5BEEFULL;
constexpr size_t kSuperBlockSize = 4096;
constexpr size_t kSuperBlockCriticalSize = 512;

#pragma pack(push, 1)
struct SuperBlockCritical {
  uint64_t magic{kSuperBlockMagic};
  uint64_t epoch{0};
  uint64_t active_root{0};
  uint64_t data_lsn{0};
  uint64_t wal_lsn{0};
  uint32_t crc32{0};
  uint8_t padding[468]{};
};
#pragma pack(pop)

struct SuperBlock {
  SuperBlockCritical critical{};
  uint64_t tlog_tail{0};
  uint32_t shard_id{0};
  uint32_t format_version{1};
  uint8_t reserved[3560]{};
  uint64_t crc64{0};
};

static_assert(sizeof(SuperBlockCritical) == 512, "SuperBlockCritical must be 512 bytes");
static_assert(sizeof(SuperBlock) == 4096, "SuperBlock must be 4096 bytes");

class SuperBlockStore {
 public:
  explicit SuperBlockStore(std::string path);

  Status Load(SuperBlock* out);
  Status Commit(const SuperBlock& block);
  Status CorruptEpochForTest();
  Status CorruptSlotForTest(int slot);

  const std::string& path() const { return path_; }

 private:
  Status ReadSlot(int slot, SuperBlock* out);
  Status WriteSlot(int slot, const SuperBlock& block);
  void FinalizeCritical(SuperBlock* block);
  bool ValidateCritical(const SuperBlockCritical& c) const;

  std::string path_;
};

}  // namespace ebtree
