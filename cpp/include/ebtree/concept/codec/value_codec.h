#pragma once

#include <cstdint>
#include <string>

#include "ebtree/common/status.h"

namespace ebtree {

enum class ValueCodec : uint8_t {
  kRaw = 0,
  kLegacyRle = 1,  // Phase 6 RLE — read-only compat
  kReserved = 2,
  kLzma7z = 3,     // 7-Zip LZMA (Phase 10 default write path)
};

struct ValueCodecResult {
  ValueCodec codec{ValueCodec::kRaw};
  std::string payload;
  uint32_t uncompressed_size{0};
};

// Compress value; returns raw if compression not beneficial.
Status CompressValue(const std::string& input, bool enable, ValueCodecResult* out);

Status DecompressValue(ValueCodec codec, const std::string& payload,
                       uint32_t uncompressed_size, std::string* out);

}  // namespace ebtree
