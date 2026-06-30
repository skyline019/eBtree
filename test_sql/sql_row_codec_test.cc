#include <gtest/gtest.h>

#include "sql/catalog/row_codec.h"

namespace ebtree {
namespace sql {
namespace {

TEST(SqlRowCodec, BinaryRoundTrip) {
  std::vector<ColumnDef> cols{{"key", "TEXT"}, {"value", "TEXT"}};
  std::unordered_map<std::string, std::string> values{{"key", "k1"}, {"value", "v1"}};
  std::string encoded;
  ASSERT_TRUE(EncodeRow(cols, 3, values, &encoded).ok());
  EXPECT_TRUE(IsBinaryRow(encoded));
  std::unordered_map<std::string, std::string> decoded;
  ASSERT_TRUE(DecodeRowFields(encoded, &decoded).ok());
  EXPECT_EQ(decoded["key"], "k1");
  EXPECT_EQ(decoded["value"], "v1");
}

TEST(SqlRowCodec, JsonBackwardCompat) {
  const std::string json = R"({"key":"k1","value":"v1"})";
  std::unordered_map<std::string, std::string> decoded;
  ASSERT_TRUE(DecodeRowFields(json, &decoded).ok());
  EXPECT_EQ(decoded["key"], "k1");
  EXPECT_EQ(decoded["value"], "v1");
}

TEST(SqlRowCodec, UpdateFieldsBinary) {
  std::vector<ColumnDef> cols{{"key", "TEXT"}, {"value", "TEXT"}};
  std::unordered_map<std::string, std::string> values{{"key", "k1"}, {"value", "v1"}};
  std::string encoded;
  ASSERT_TRUE(EncodeRow(cols, 3, values, &encoded).ok());
  std::string updated;
  ASSERT_TRUE(UpdateRowFields(encoded, "value", "v2", &updated).ok());
  std::unordered_map<std::string, std::string> decoded;
  ASSERT_TRUE(DecodeRowFields(updated, &decoded).ok());
  EXPECT_EQ(decoded["value"], "v2");
}

}  // namespace
}  // namespace sql
}  // namespace ebtree
