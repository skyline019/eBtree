#include "ebtree/concept/gc/region_manager.h"

#include <filesystem>

#include "ebtree/common/crc32.h"

namespace ebtree {

RegionManager::RegionManager(std::string path) : path_(std::move(path)) {
  std::filesystem::create_directories(
      std::filesystem::path(path_).parent_path());
}

Status RegionManager::Load() {
  GcMeta a{};
  GcMeta b{};
  const Status sa = ReadSlot(0, &a);
  const Status sb = ReadSlot(1, &b);
  if (!sa.ok() && !sb.ok()) {
    active_generation_ = 0;
    reclaim_generation_ = 0xFF;
    epoch_ = 0;
    return Status::Ok();
  }

  const bool va = sa.ok() && ValidateCritical(a.critical);
  const bool vb = sb.ok() && ValidateCritical(b.critical);
  if (va && vb) {
    if (a.critical.epoch >= b.critical.epoch) {
      active_generation_ = a.critical.active_generation;
      reclaim_generation_ = a.critical.reserved[0];
      epoch_ = a.critical.epoch;
    } else {
      active_generation_ = b.critical.active_generation;
      reclaim_generation_ = b.critical.reserved[0];
      epoch_ = b.critical.epoch;
    }
    return Status::Ok();
  }
  if (va) {
    active_generation_ = a.critical.active_generation;
    reclaim_generation_ = a.critical.reserved[0];
    epoch_ = a.critical.epoch;
    return Status::Ok();
  }
  if (vb) {
    active_generation_ = b.critical.active_generation;
    reclaim_generation_ = b.critical.reserved[0];
    epoch_ = b.critical.epoch;
    return Status::Ok();
  }
  active_generation_ = 0;
  reclaim_generation_ = 0xFF;
  epoch_ = 0;
  return Status::Ok();
}

Status RegionManager::SwapRegions() {
  reclaim_generation_ = active_generation_;
  active_generation_ = static_cast<uint8_t>(1 - active_generation_);
  ++epoch_;

  GcMeta block{};
  block.critical.active_generation = active_generation_;
  block.critical.reserved[0] = reclaim_generation_;
  block.critical.epoch = epoch_;
  FinalizeCritical(&block);

  const int slot = static_cast<int>(epoch_ % 2);
  return WriteSlot(slot, block);
}

Status RegionManager::MaybeSwap(uint64_t datafile_bytes, uint64_t threshold_bytes) {
  if (threshold_bytes == 0 || datafile_bytes < threshold_bytes) {
    return Status::Ok();
  }
  return SwapRegions();
}

Status RegionManager::ReadSlot(int slot, GcMeta* out) {
  if (!out) return Status::InvalidArgument("out is null");
  std::ifstream in(path_, std::ios::binary);
  if (!in) return Status::IoError("gcmeta open failed");
  in.seekg(static_cast<std::streamoff>(slot) * sizeof(GcMeta));
  in.read(reinterpret_cast<char*>(out), sizeof(GcMeta));
  if (!in) return Status::IoError("gcmeta read failed");
  return Status::Ok();
}

Status RegionManager::WriteSlot(int slot, const GcMeta& block) {
  std::fstream out(path_, std::ios::binary | std::ios::in | std::ios::out);
  if (!out) {
    std::ofstream create(path_, std::ios::binary);
    create.seekp(static_cast<std::streamoff>(slot + 1) * sizeof(GcMeta) - 1);
    create.put('\0');
    create.close();
    out.open(path_, std::ios::binary | std::ios::in | std::ios::out);
  }
  if (!out) return Status::IoError("gcmeta write open failed");
  out.seekp(static_cast<std::streamoff>(slot) * sizeof(GcMeta));
  out.write(reinterpret_cast<const char*>(&block), sizeof(GcMeta));
  out.flush();
  if (!out) return Status::IoError("gcmeta write failed");
  return Status::Ok();
}

void RegionManager::FinalizeCritical(GcMeta* block) {
  if (!block) return;
  block->critical.magic = 0xEB710CULL;
  block->critical.crc32 =
      Crc32(&block->critical, sizeof(block->critical) - sizeof(block->critical.crc32));
}

bool RegionManager::ValidateCritical(const GcMetaCritical& c) const {
  if (c.magic != 0xEB710CULL) return false;
  const uint32_t expected =
      Crc32(&c, sizeof(c) - sizeof(c.crc32));
  return c.crc32 == expected;
}

}  // namespace ebtree
