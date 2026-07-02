#pragma once

#include <string>

#include "ebtree/common/status.h"

namespace ebtree {

struct Lz4FastResult {
  bool compressed{false};
  std::string payload;
  uint32_t uncompressed_size{0};
};

// Fast byte-run packer for repetitive payloads (LZ4-fast wire id 2).
Status Lz4FastCompress(const std::string& input, Lz4FastResult* out);
Status Lz4FastDecompress(const std::string& payload, uint32_t uncompressed_size,
                         std::string* out);

}  // namespace ebtree
