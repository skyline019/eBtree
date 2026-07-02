#include "ebtree/concept/codec/value_codec.h"

#include "ebtree/concept/codec/codec_registry.h"
#include "ebtree/concept/codec/lz4_fast_codec.h"
#include "ebtree/concept/codec/lzma_codec.h"

#include <algorithm>
#include <cstring>
#include <vector>

namespace ebtree {

namespace {

constexpr uint8_t kEsc = 0xFF;

Status SimpleDecompress(const std::string& payload, uint32_t expected,
                        std::string* out) {
  if (!out) return Status::InvalidArgument("out is null");
  out->clear();
  out->reserve(expected);
  size_t pos = 0;
  while (pos < payload.size() && out->size() < expected) {
    if (payload[pos] != static_cast<char>(kEsc)) {
      return Status::CorruptPage("value codec corrupt");
    }
    ++pos;
    if (pos >= payload.size()) return Status::CorruptPage("value codec trunc");
    const uint8_t tag = static_cast<uint8_t>(payload[pos++]);
    if (tag >= 4 && pos < payload.size()) {
      const char byte = payload[pos++];
      for (uint8_t n = 0; n < tag; ++n) out->push_back(byte);
    } else {
      const size_t lit_len = tag;
      if (pos + lit_len > payload.size()) {
        return Status::CorruptPage("value codec literal trunc");
      }
      out->append(payload.data() + pos, lit_len);
      pos += lit_len;
    }
  }
  if (out->size() != expected) {
    return Status::CorruptPage("value codec size mismatch");
  }
  return Status::Ok();
}

ValueCodec WireCodec(uint8_t wire) {
  switch (wire) {
    case 0: return ValueCodec::kRaw;
    case 1: return ValueCodec::kLegacyRle;
    case 2: return ValueCodec::kLz4Fast;
    case 3: return ValueCodec::kLzma7z;
    case 4: return ValueCodec::kZstdFast;
    default: return ValueCodec::kRaw;
  }
}

}  // namespace

Status CompressValue(const std::string& input, bool enable,
                     ValueCodecResult* out) {
  const CompressPolicy policy =
      enable ? CompressPolicy::kBalanced : CompressPolicy::kOff;
  return CodecRegistry::CompressValue(input, policy, enable, out);
}

Status DecompressValue(ValueCodec codec, const std::string& payload,
                       uint32_t uncompressed_size, std::string* out) {
  if (!out) return Status::InvalidArgument("out is null");
  if (codec == ValueCodec::kLegacyRle) {
    return SimpleDecompress(payload, uncompressed_size, out);
  }
  return CodecRegistry::DecompressValue(codec, payload, uncompressed_size, out);
}

}  // namespace ebtree
