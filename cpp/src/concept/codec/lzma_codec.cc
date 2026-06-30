#include "ebtree/concept/codec/lzma_codec.h"

#include <algorithm>
#include <cstring>
#include <vector>

extern "C" {
#include "LzmaLib.h"
}

namespace ebtree {

namespace {

constexpr size_t kLzmaPropsSize = LZMA_PROPS_SIZE;

void GetPresetParams(LzmaPreset preset, int* level, unsigned* dict_size) {
  if (preset == LzmaPreset::kPageBlock) {
    *level = 5;
    *dict_size = 1u << 20;
  } else {
    *level = 1;
    *dict_size = 1u << 18;
  }
}

Status PackLzmaPayload(const std::string& compressed, uint32_t uncompressed_size,
                       const unsigned char props[kLzmaPropsSize],
                       std::string* out) {
  if (!out) return Status::InvalidArgument("out is null");
  out->clear();
  out->reserve(4 + kLzmaPropsSize + compressed.size());
  const uint32_t usize = uncompressed_size;
  out->push_back(static_cast<char>(usize & 0xFF));
  out->push_back(static_cast<char>((usize >> 8) & 0xFF));
  out->push_back(static_cast<char>((usize >> 16) & 0xFF));
  out->push_back(static_cast<char>((usize >> 24) & 0xFF));
  out->append(reinterpret_cast<const char*>(props), kLzmaPropsSize);
  out->append(compressed);
  return Status::Ok();
}

}  // namespace

Status LzmaCompressPreset(LzmaPreset preset, const std::string& input,
                          LzmaCodecResult* out) {
  if (!out) return Status::InvalidArgument("out is null");
  out->compressed = false;
  out->payload = input;
  out->uncompressed_size = static_cast<uint32_t>(input.size());
  if (input.empty()) return Status::Ok();

  int level = 1;
  unsigned dict_size = 1u << 18;
  GetPresetParams(preset, &level, &dict_size);

  const size_t src_len = input.size();
  size_t dest_cap = src_len + src_len / 3 + 128;
  std::vector<unsigned char> dest(dest_cap);
  size_t dest_len = dest_cap;
  unsigned char props[kLzmaPropsSize]{};
  size_t props_size = kLzmaPropsSize;

  const int rc = LzmaCompress(dest.data(), &dest_len,
                                reinterpret_cast<const unsigned char*>(input.data()),
                                src_len, props, &props_size, level, dict_size,
                                3, 0, 2, 32, 1);
  if (rc != SZ_OK) {
    return Status::Internal("LzmaCompress failed");
  }

  std::string wire;
  const Status ps = PackLzmaPayload(
      std::string(reinterpret_cast<char*>(dest.data()), dest_len),
      static_cast<uint32_t>(src_len), props, &wire);
  if (!ps.ok()) return ps;

  if (wire.size() >= input.size()) return Status::Ok();

  out->compressed = true;
  out->payload = std::move(wire);
  out->uncompressed_size = static_cast<uint32_t>(src_len);
  return Status::Ok();
}

Status LzmaDecompressPayload(const std::string& payload,
                             uint32_t uncompressed_size, std::string* out) {
  if (!out) return Status::InvalidArgument("out is null");
  if (payload.size() < 4 + kLzmaPropsSize) {
    return Status::CorruptPage("lzma payload trunc");
  }
  const auto* u = reinterpret_cast<const unsigned char*>(payload.data());
  const uint32_t usize = static_cast<uint32_t>(u[0]) |
                         (static_cast<uint32_t>(u[1]) << 8) |
                         (static_cast<uint32_t>(u[2]) << 16) |
                         (static_cast<uint32_t>(u[3]) << 24);
  if (usize != uncompressed_size) {
    return Status::CorruptPage("lzma size mismatch");
  }
  const unsigned char* props = u + 4;
  const unsigned char* src = u + 4 + kLzmaPropsSize;
  const size_t src_len = payload.size() - 4 - kLzmaPropsSize;

  std::vector<unsigned char> dest(uncompressed_size);
  size_t dest_len = uncompressed_size;
  SizeT src_processed = static_cast<SizeT>(src_len);
  const int rc = LzmaUncompress(dest.data(), &dest_len, src, &src_processed,
                                props, kLzmaPropsSize);
  if (rc != SZ_OK || dest_len != uncompressed_size) {
    return Status::CorruptPage("lzma decompress failed");
  }
  out->assign(reinterpret_cast<char*>(dest.data()), dest_len);
  return Status::Ok();
}

}  // namespace ebtree
