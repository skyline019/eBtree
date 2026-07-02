#include "ebtree/concept/codec/codec_registry.h"

#include "ebtree/concept/codec/lz4_fast_codec.h"
#include "ebtree/concept/codec/lzma_codec.h"

namespace ebtree {

namespace {

thread_local CompressStatsSnapshot g_compress_stats{};

Status TryLzmaFast(const std::string& input, ValueCodecResult* out) {
  LzmaCodecResult lz{};
  const Status st = LzmaCompressPreset(LzmaPreset::kFastValue, input, &lz);
  if (!st.ok()) return st;
  if (!lz.compressed) return Status::Ok();
  out->codec = ValueCodec::kLzma7z;
  out->payload = std::move(lz.payload);
  out->uncompressed_size = lz.uncompressed_size;
  return Status::Ok();
}

}  // namespace

void CodecRegistry::RecordCompressChoice(ValueCodec codec, size_t raw_len,
                                         size_t wire_len,
                                         CompressStatsSnapshot* stats) {
  CompressStatsSnapshot* target = stats ? stats : &g_compress_stats;
  switch (codec) {
    case ValueCodec::kRaw:
      ++target->raw_total;
      break;
    case ValueCodec::kLz4Fast:
      ++target->lz4_fast_total;
      break;
    case ValueCodec::kLzma7z:
      ++target->lzma_total;
      break;
    case ValueCodec::kZstdFast:
      ++target->zstd_fast_total;
      break;
    default:
      ++target->raw_total;
      break;
  }
  if (wire_len < raw_len) {
    target->bytes_saved += (raw_len - wire_len);
  }
}

Status CodecRegistry::CompressValue(const std::string& input,
                                    CompressPolicy policy, bool enable,
                                    ValueCodecResult* out) {
  if (!out) return Status::InvalidArgument("out is null");
  out->codec = ValueCodec::kRaw;
  out->payload = input;
  out->uncompressed_size = static_cast<uint32_t>(input.size());
  if (!enable || policy == CompressPolicy::kOff || input.size() < 64) {
    return Status::Ok();
  }

  if (policy == CompressPolicy::kFastOnly || policy == CompressPolicy::kBalanced ||
      policy == CompressPolicy::kAuto) {
    Lz4FastResult fast{};
    const Status fs = Lz4FastCompress(input, &fast);
    if (fs.ok() && fast.compressed && fast.payload.size() < input.size()) {
      out->codec = ValueCodec::kLz4Fast;
      out->payload = std::move(fast.payload);
      out->uncompressed_size = fast.uncompressed_size;
      RecordCompressChoice(out->codec, input.size(), out->payload.size(), nullptr);
      return Status::Ok();
    }
  }

  if (policy == CompressPolicy::kBalanced || policy == CompressPolicy::kDense ||
      policy == CompressPolicy::kAuto) {
    ValueCodecResult lz{};
    const Status ls = TryLzmaFast(input, &lz);
    if (ls.ok() && lz.codec == ValueCodec::kLzma7z &&
        lz.payload.size() < input.size()) {
      if (policy == CompressPolicy::kBalanced) {
        lz.codec = ValueCodec::kZstdFast;
      }
      *out = std::move(lz);
      RecordCompressChoice(out->codec, input.size(), out->payload.size(), nullptr);
      return Status::Ok();
    }
  }

  if (policy == CompressPolicy::kDense) {
    ValueCodecResult lz{};
    const Status ls = TryLzmaFast(input, &lz);
    if (ls.ok() && lz.codec == ValueCodec::kLzma7z) {
      *out = std::move(lz);
      RecordCompressChoice(out->codec, input.size(), out->payload.size(), nullptr);
    }
  }
  return Status::Ok();
}

Status CodecRegistry::DecompressValue(ValueCodec codec, const std::string& payload,
                                      uint32_t uncompressed_size,
                                      std::string* out) {
  if (!out) return Status::InvalidArgument("out is null");
  if (codec == ValueCodec::kRaw) {
    *out = payload;
    return Status::Ok();
  }
  if (codec == ValueCodec::kLz4Fast || codec == ValueCodec::kZstdFast) {
    if (codec == ValueCodec::kLz4Fast) {
      return Lz4FastDecompress(payload, uncompressed_size, out);
    }
    return LzmaDecompressPayload(payload, uncompressed_size, out);
  }
  if (codec == ValueCodec::kLzma7z) {
    return LzmaDecompressPayload(payload, uncompressed_size, out);
  }
  if (codec == ValueCodec::kLegacyRle) {
    return Status::InvalidArgument("legacy rle via registry");
  }
  *out = payload;
  return Status::Ok();
}

}  // namespace ebtree
