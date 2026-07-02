#include <gtest/gtest.h>

#include <string>

#include "ebtree/concept/codec/codec_registry.h"
#include "ebtree/concept/codec/value_codec.h"

namespace ebtree {
namespace {

TEST(ValueCodec, RoundTripRawSmall) {
  const std::string input = "short";
  ValueCodecResult cr{};
  ASSERT_TRUE(CompressValue(input, true, &cr).ok());
  EXPECT_EQ(cr.codec, ValueCodec::kRaw);
  std::string out;
  ASSERT_TRUE(DecompressValue(cr.codec, cr.payload, cr.uncompressed_size, &out).ok());
  EXPECT_EQ(out, input);
}

TEST(ValueCodec, RoundTripLzmaBlock) {
  std::string input(128, 'x');
  input += std::string(64, 'y');
  ValueCodecResult cr{};
  ASSERT_TRUE(
      CodecRegistry::CompressValue(input, CompressPolicy::kDense, true, &cr).ok());
  EXPECT_EQ(cr.codec, ValueCodec::kLzma7z);
  std::string out;
  ASSERT_TRUE(DecompressValue(cr.codec, cr.payload, cr.uncompressed_size, &out).ok());
  EXPECT_EQ(out, input);
}

TEST(ValueCodec, DisabledStaysRaw) {
  std::string input(128, 'a');
  ValueCodecResult cr{};
  ASSERT_TRUE(CompressValue(input, false, &cr).ok());
  EXPECT_EQ(cr.codec, ValueCodec::kRaw);
}

}  // namespace
}  // namespace ebtree
