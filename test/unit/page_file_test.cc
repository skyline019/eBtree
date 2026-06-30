#include <gtest/gtest.h>

#include "ebtree/concept/page/page_file.h"
#include "ebtree/concept/page/page_format.h"
#include "engine_test_util.h"

TEST(EbPageFile, PersistentReadHandleReusesOpen) {
  const std::string dir = ebtree::test::TempDir("pagefile_read");
  const std::string path = dir + "/test.pages";
  ebtree::PageFile pf(path);
  std::vector<uint8_t> page(ebtree::kPageSize, 0xAB);
  uint64_t off = 0;
  ASSERT_TRUE(pf.AppendPage(page.data(), page.size(), &off).ok());
  std::vector<uint8_t> read1(ebtree::kPageSize, 0);
  std::vector<uint8_t> read2(ebtree::kPageSize, 0);
  ASSERT_TRUE(pf.ReadPage(off, read1.data(), read1.size()).ok());
  ASSERT_TRUE(pf.ReadPage(off, read2.data(), read2.size()).ok());
  EXPECT_EQ(1u, pf.read_opens_for_test());
  EXPECT_EQ(0xAB, read1[sizeof(ebtree::PageHeader)]);
}

TEST(EbPageFile, BatchReadPages) {
  const std::string dir = ebtree::test::TempDir("pagefile_batch");
  const std::string path = dir + "/batch.pages";
  ebtree::PageFile pf(path);
  std::vector<uint8_t> page(ebtree::kPageSize, 0xCD);
  uint64_t off = 0;
  ASSERT_TRUE(pf.AppendPage(page.data(), page.size(), &off).ok());
  std::vector<std::vector<uint8_t>> pages;
  ASSERT_TRUE(pf.ReadPages({off}, &pages).ok());
  ASSERT_EQ(1u, pages.size());
  EXPECT_EQ(0xCD, pages[0][sizeof(ebtree::PageHeader)]);
}

TEST(EbPageFile, LzmaWrappedPagesRoundTrip) {
  const std::string dir = ebtree::test::TempDir("pagefile_lzma");
  const std::string path = dir + "/lzma.pages";
  ebtree::PageFile pf(path);
  pf.SetCompressPages(true);
  std::vector<uint8_t> page(ebtree::kPageSize, 0);
  for (size_t i = 0; i < page.size(); ++i) {
    page[i] = static_cast<uint8_t>(i % 251);
  }
  uint64_t off = 0;
  ASSERT_TRUE(pf.AppendPage(page.data(), page.size(), &off).ok());
  EXPECT_TRUE(pf.wrapped_format());
  std::vector<uint8_t> read(ebtree::kPageSize, 0);
  ASSERT_TRUE(pf.ReadPage(off, read.data(), read.size()).ok());
  EXPECT_EQ(page, read);
}
