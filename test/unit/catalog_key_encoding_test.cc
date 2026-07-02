#include <gtest/gtest.h>

#include "sql/catalog/catalog.h"

namespace ebtree {
namespace sql {
namespace {

TEST(CatalogKeyEncoding, IsIndexEncodedKeyRejectsRowKeyWithPkI) {
  EXPECT_FALSE(Catalog::IsIndexEncodedKey("1:i"));
  EXPECT_FALSE(Catalog::IsIndexEncodedKey("1:ia"));
  EXPECT_FALSE(Catalog::IsIndexEncodedKey("10:item"));
}

TEST(CatalogKeyEncoding, IsIndexEncodedKeyAcceptsIndexKeys) {
  EXPECT_TRUE(Catalog::IsIndexEncodedKey("1:i1:nine:i"));
  EXPECT_TRUE(Catalog::IsIndexEncodedKey("1:i12:val:pk"));
  EXPECT_TRUE(Catalog::IsIndexEncodedKey("10:i3:seven:h"));
}

TEST(CatalogKeyEncoding, IsIndexEncodedKeyRejectsMalformed) {
  EXPECT_FALSE(Catalog::IsIndexEncodedKey("1:h"));
  EXPECT_FALSE(Catalog::IsIndexEncodedKey("1:i"));
  EXPECT_FALSE(Catalog::IsIndexEncodedKey("1:i:"));
  EXPECT_FALSE(Catalog::IsIndexEncodedKey("1:ix:val:pk"));
}

TEST(CatalogKeyEncoding, EncodeRowKeyRoundTrip) {
  Catalog catalog;
  const std::string encoded = catalog.EncodeRowKey(1, "i");
  EXPECT_FALSE(Catalog::IsIndexEncodedKey(encoded));
  uint32_t table_id = 0;
  std::string pk;
  ASSERT_TRUE(catalog.DecodeRowKey(encoded, &table_id, &pk));
  EXPECT_EQ(table_id, 1u);
  EXPECT_EQ(pk, "i");
}

}  // namespace
}  // namespace sql
}  // namespace ebtree
