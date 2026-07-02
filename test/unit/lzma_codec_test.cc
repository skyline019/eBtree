#include <gtest/gtest.h>

#include <string>

#include "ebtree/concept/codec/lzma_codec.h"
#include "ebtree/concept/codec/codec_registry.h"
#include "ebtree/concept/codec/value_codec.h"

namespace ebtree {
namespace {

TEST(LzmaCodec, RoundTripFastValue) {
  std::string input(256, 'x');
  input += std::string(128, 'y');
  LzmaCodecResult cr{};
  ASSERT_TRUE(LzmaCompressPreset(LzmaPreset::kFastValue, input, &cr).ok());
  ASSERT_TRUE(cr.compressed);
  std::string out;
  ASSERT_TRUE(LzmaDecompressPayload(cr.payload, cr.uncompressed_size, &out).ok());
  EXPECT_EQ(out, input);
}

TEST(LzmaCodec, EmptyStaysUncompressed) {
  LzmaCodecResult cr{};
  ASSERT_TRUE(LzmaCompressPreset(LzmaPreset::kFastValue, "", &cr).ok());
  EXPECT_FALSE(cr.compressed);
}

TEST(LzmaCodec, TruncatedPayloadCorrupt) {
  std::string out;
  EXPECT_FALSE(LzmaDecompressPayload("abc", 10, &out).ok());
}

TEST(ValueCodec, RoundTripLzma7z) {
  std::string input(200, 'a');
  for (int i = 0; i < 50; ++i) input.push_back(static_cast<char>('0' + (i % 10)));
  ValueCodecResult cr{};
  ASSERT_TRUE(
      CodecRegistry::CompressValue(input, CompressPolicy::kDense, true, &cr).ok());
  EXPECT_EQ(cr.codec, ValueCodec::kLzma7z);
  std::string out;
  ASSERT_TRUE(DecompressValue(cr.codec, cr.payload, cr.uncompressed_size, &out).ok());
  EXPECT_EQ(out, input);
}

TEST(ValueCodec, LegacyRleReadOnly) {
  std::string input(128, 'x');
  input += std::string(64, 'y');
  ValueCodecResult enc{};
  enc.codec = ValueCodec::kLegacyRle;
  enc.uncompressed_size = static_cast<uint32_t>(input.size());
  enc.payload = input;
  std::string wire;
  wire.push_back(static_cast<char>(enc.uncompressed_size & 0xFF));
  wire.push_back(static_cast<char>((enc.uncompressed_size >> 8) & 0xFF));
  wire.push_back(static_cast<char>((enc.uncompressed_size >> 16) & 0xFF));
  wire.push_back(static_cast<char>((enc.uncompressed_size >> 24) & 0xFF));
  std::string rle;
  rle.push_back(static_cast<char>(0xFF));
  rle.push_back(static_cast<char>(128));
  rle.push_back('x');
  rle.push_back(static_cast<char>(0xFF));
  rle.push_back(static_cast<char>(64));
  rle.push_back('y');
  std::string out;
  ASSERT_TRUE(
      DecompressValue(ValueCodec::kLegacyRle, rle, enc.uncompressed_size, &out).ok());
  EXPECT_EQ(out, input);
}

}  // namespace
}  // namespace ebtree
