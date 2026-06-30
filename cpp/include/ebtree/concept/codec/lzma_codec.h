#pragma once

#include <cstdint>
#include <string>

#include "ebtree/common/status.h"

namespace ebtree {

enum class LzmaPreset {
  kFastValue,  // level=1, dict=256KB — DataFile values
  kPageBlock,  // level=5, dict=1MB — page blocks
};

struct LzmaCodecResult {
  bool compressed{false};
  std::string payload;
  uint32_t uncompressed_size{0};
};

// Wire payload when compressed: [u32 uncompressed_size][5-byte LZMA props][bytes...]
Status LzmaCompressPreset(LzmaPreset preset, const std::string& input,
                          LzmaCodecResult* out);

Status LzmaDecompressPayload(const std::string& payload,
                             uint32_t uncompressed_size, std::string* out);

}  // namespace ebtree
