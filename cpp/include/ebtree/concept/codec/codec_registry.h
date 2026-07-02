#pragma once

#include <cstdint>
#include <string>

#include "ebtree/common/status.h"
#include "ebtree/concept/codec/value_codec.h"

namespace ebtree {

enum class CompressPolicy {
  kOff,
  kFastOnly,
  kBalanced,
  kDense,
  kAuto,
};

struct CompressStatsSnapshot {
  uint64_t raw_total{0};
  uint64_t lz4_fast_total{0};
  uint64_t lzma_total{0};
  uint64_t zstd_fast_total{0};
  uint64_t bytes_saved{0};
  uint64_t decompress_fail{0};
};

class CodecRegistry {
 public:
  static Status CompressValue(const std::string& input, CompressPolicy policy,
                              bool enable, ValueCodecResult* out);
  static Status DecompressValue(ValueCodec codec, const std::string& payload,
                                uint32_t uncompressed_size, std::string* out);
  static void RecordCompressChoice(ValueCodec codec, size_t raw_len,
                                   size_t wire_len, CompressStatsSnapshot* stats);
};

}  // namespace ebtree
