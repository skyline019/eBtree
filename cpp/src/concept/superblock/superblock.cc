#include "ebtree/concept/superblock/superblock.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <filesystem>

namespace ebtree {
namespace {

void ZeroPadding(SuperBlock* b) {
  std::memset(b->critical.padding, 0, sizeof(b->critical.padding));
  std::memset(b->reserved, 0, sizeof(b->reserved));
}

}  // namespace

SuperBlockStore::SuperBlockStore(std::string path) : path_(std::move(path)) {}

bool SuperBlockStore::ValidateCritical(const SuperBlockCritical& c) const {
  if (c.magic != kSuperBlockMagic) {
    return false;
  }
  SuperBlockCritical tmp = c;
  tmp.crc32 = 0;
  return Crc32(&tmp, kSuperBlockCriticalSize) == c.crc32;
}

void SuperBlockStore::FinalizeCritical(SuperBlock* block) {
  ZeroPadding(block);
  block->critical.crc32 = 0;
  block->critical.crc32 = Crc32(&block->critical, kSuperBlockCriticalSize);
  block->crc64 = Crc32(block, kSuperBlockSize - sizeof(block->crc64));
}

Status SuperBlockStore::ReadSlot(int slot, SuperBlock* out) {
  std::ifstream in(path_, std::ios::binary);
  if (!in) {
    return Status::IoError("cannot open superblock: " + path_);
  }
  in.seekg(static_cast<std::streamoff>(slot) * kSuperBlockSize);
  in.read(reinterpret_cast<char*>(out), kSuperBlockSize);
  if (!in) {
    return Status::IoError("superblock read short");
  }
  const uint64_t stored = out->crc64;
  out->crc64 = 0;
  if (Crc32(out, kSuperBlockSize - sizeof(out->crc64)) != stored) {
    return Status::Corrupt("superblock crc64 mismatch");
  }
  out->crc64 = stored;
  if (!ValidateCritical(out->critical)) {
    return Status::Corrupt("superblock critical crc32 mismatch");
  }
  return Status::Ok();
}

Status SuperBlockStore::WriteSlot(int slot, const SuperBlock& block) {
  std::filesystem::create_directories(
      std::filesystem::path(path_).parent_path());
  std::fstream io(path_, std::ios::binary | std::ios::in | std::ios::out);
  if (!io) {
    std::ofstream create(path_, std::ios::binary);
    if (!create) {
      return Status::IoError("cannot create superblock: " + path_);
    }
    SuperBlock zero{};
    create.write(reinterpret_cast<const char*>(&zero), kSuperBlockSize);
    create.write(reinterpret_cast<const char*>(&zero), kSuperBlockSize);
    create.close();
    io.open(path_, std::ios::binary | std::ios::in | std::ios::out);
  }
  io.seekp(static_cast<std::streamoff>(slot) * kSuperBlockSize);
  io.write(reinterpret_cast<const char*>(&block), kSuperBlockSize);
  io.flush();
  if (!io) {
    return Status::IoError("superblock write failed");
  }
  return Status::Ok();
}

Status SuperBlockStore::Load(SuperBlock* out) {
  if (!std::filesystem::exists(path_)) {
    *out = SuperBlock{};
    return Status::Ok();
  }
  SuperBlock a{};
  SuperBlock b{};
  const Status sa = ReadSlot(0, &a);
  const Status sb = ReadSlot(1, &b);
  const bool ok_a = sa.ok();
  const bool ok_b = sb.ok();
  if (!ok_a && !ok_b) {
    if (sa.code() == StatusCode::kIoError) {
      return sa;
    }
    return Status::Corrupt("CorruptSuperBlock");
  }
  if (ok_a && (!ok_b || a.critical.epoch >= b.critical.epoch)) {
    *out = a;
    return Status::Ok();
  }
  if (ok_b) {
    *out = b;
    return Status::Ok();
  }
  return Status::Corrupt("CorruptSuperBlock");
}

Status SuperBlockStore::Commit(const SuperBlock& in) {
  SuperBlock current{};
  (void)Load(&current);
  SuperBlock next = in;
  next.critical.epoch = std::max(current.critical.epoch, in.critical.epoch) + 1;
  FinalizeCritical(&next);
  const int inactive = (current.critical.epoch % 2 == 0) ? 1 : 0;
  return WriteSlot(inactive, next);
}

Status SuperBlockStore::CorruptSlotForTest(int slot) {
  SuperBlock block{};
  (void)Load(&block);
  block.critical.epoch = 0xDEADBEEFULL;
  block.critical.crc32 = 0;
  block.crc64 = 0;
  return WriteSlot(slot, block);
}

Status SuperBlockStore::CorruptEpochForTest() {
  SuperBlock block{};
  (void)Load(&block);
  block.critical.epoch = 0xDEADBEEFULL;
  block.critical.crc32 = 0;
  block.crc64 = 0;
  const Status a = WriteSlot(0, block);
  const Status b = WriteSlot(1, block);
  if (!a.ok()) return a;
  return b;
}

}  // namespace ebtree
