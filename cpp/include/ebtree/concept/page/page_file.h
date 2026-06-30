#pragma once

#include <cstdint>
#include <fstream>
#include <list>
#include <string>
#include <unordered_map>
#include <vector>

#include "ebtree/common/status.h"
#include "ebtree/concept/page/page_format.h"

namespace ebtree {

class PageFile {
 public:
  explicit PageFile(std::string path);
  ~PageFile();

  PageFile(const PageFile&) = delete;
  PageFile& operator=(const PageFile&) = delete;

  void SetCacheCapacity(size_t pages);
  void SetCompressPages(bool enable);

  Status AppendPage(const uint8_t* data, size_t len, uint64_t* offset_out);
  Status AppendPages(const std::vector<const uint8_t*>& pages,
                     uint64_t* first_offset_out);
  Status ReadPage(uint64_t offset, uint8_t* data, size_t len) const;
  Status ReadPages(const std::vector<uint64_t>& offsets,
                   std::vector<std::vector<uint8_t>>* pages_out) const;

  uint64_t next_offset() const { return next_offset_; }
  uint64_t append_offset() const { return next_offset_; }
  const std::string& path() const { return path_; }
  bool compress_pages() const { return compress_pages_; }
  bool wrapped_format() const { return wrapped_format_; }

  uint64_t read_opens_for_test() const { return read_opens_; }

 private:
  Status LoadHeader();
  Status SaveHeader();
  Status EnsureReadFileOpen() const;
  Status ReadPageUncached(uint64_t offset, uint8_t* data, size_t len) const;
  Status ReadStoredPage(uint64_t offset, std::vector<uint8_t>* logical_page) const;
  Status WriteStoredPage(std::fstream* file, const uint8_t* data, size_t len,
                         uint64_t* bytes_written);

  std::string path_;
  uint64_t next_offset_{kPageFileHeaderSize};
  bool compress_pages_{false};
  bool wrapped_format_{false};
  mutable std::fstream read_file_;
  mutable uint64_t read_opens_{0};
  mutable size_t cache_capacity_{64};
  mutable std::list<uint64_t> cache_lru_;
  mutable std::unordered_map<uint64_t, std::list<uint64_t>::iterator> cache_map_;
  mutable std::unordered_map<uint64_t, std::vector<uint8_t>> cache_pages_;
};

}  // namespace ebtree
