#include "ebtree/concept/page/page_file.h"

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <unordered_map>
#include <vector>

#include "ebtree/common/crc32.h"
#include "ebtree/concept/codec/lzma_codec.h"

namespace ebtree {

namespace {

constexpr uint64_t kPageFileMagic = 0xEB50414745524C;  // "EBPAGEFL"
constexpr uint32_t kPageFileFlagWrapped = 1u;
constexpr uint8_t kPageCodecRaw = 0;
constexpr uint8_t kPageCodecLzma = 3;
constexpr size_t kStoredPageHeaderSize = 5;

struct PageFileHeader {
  uint64_t magic{kPageFileMagic};
  uint64_t next_offset{kPageFileHeaderSize};
  uint32_t format_flags{0};
};

}  // namespace

PageFile::PageFile(std::string path) : path_(std::move(path)) {
  std::filesystem::create_directories(
      std::filesystem::path(path_).parent_path());
  (void)LoadHeader();
}

PageFile::~PageFile() {
  if (read_file_.is_open()) read_file_.close();
}

void PageFile::SetCacheCapacity(size_t pages) { cache_capacity_ = pages; }

void PageFile::SetCompressPages(bool enable) {
  compress_pages_ = enable;
  if (enable) wrapped_format_ = true;
}

Status PageFile::LoadHeader() {
  std::ifstream in(path_, std::ios::binary);
  if (!in) {
    next_offset_ = kPageFileHeaderSize;
    return Status::Ok();
  }
  PageFileHeader hdr{};
  in.read(reinterpret_cast<char*>(&hdr), sizeof(hdr));
  if (!in || hdr.magic != kPageFileMagic) {
    next_offset_ = kPageFileHeaderSize;
    return Status::Ok();
  }
  next_offset_ = hdr.next_offset;
  wrapped_format_ = (hdr.format_flags & kPageFileFlagWrapped) != 0;
  return Status::Ok();
}

Status PageFile::SaveHeader() {
  std::fstream file(path_, std::ios::binary | std::ios::in | std::ios::out);
  if (!file) {
    std::ofstream create(path_, std::ios::binary);
    if (!create) return Status::IoError("pagefile create failed");
    create.close();
    file.open(path_, std::ios::binary | std::ios::in | std::ios::out);
    if (!file) return Status::IoError("pagefile open failed");
  }
  PageFileHeader hdr{};
  hdr.magic = kPageFileMagic;
  hdr.next_offset = next_offset_;
  if (wrapped_format_) hdr.format_flags |= kPageFileFlagWrapped;
  file.seekp(0);
  file.write(reinterpret_cast<const char*>(&hdr), sizeof(hdr));
  if (!file) return Status::IoError("pagefile header write failed");
  return Status::Ok();
}

Status PageFile::WriteStoredPage(std::fstream* file, const uint8_t* data,
                                 size_t len, uint64_t* bytes_written) {
  if (!file || !data || len != kPageSize) {
    return Status::InvalidArgument("bad page write");
  }
  if (!wrapped_format_) {
    file->write(reinterpret_cast<const char*>(data),
                static_cast<std::streamsize>(kPageSize));
    if (!*file) return Status::IoError("pagefile append failed");
    if (bytes_written) *bytes_written = kPageSize;
    return Status::Ok();
  }

  std::string payload(reinterpret_cast<const char*>(data), kPageSize);
  uint8_t codec = kPageCodecRaw;
  if (compress_pages_) {
    LzmaCodecResult lz{};
    const Status cs =
        LzmaCompressPreset(LzmaPreset::kPageBlock, payload, &lz);
    if (cs.ok() && lz.compressed && lz.payload.size() < payload.size()) {
      payload = lz.payload;
      codec = kPageCodecLzma;
    }
  }

  const uint32_t total_size =
      static_cast<uint32_t>(kStoredPageHeaderSize + payload.size());
  const char hdr[kStoredPageHeaderSize] = {
      static_cast<char>(total_size & 0xFF),
      static_cast<char>((total_size >> 8) & 0xFF),
      static_cast<char>((total_size >> 16) & 0xFF),
      static_cast<char>((total_size >> 24) & 0xFF),
      static_cast<char>(codec),
  };
  file->write(hdr, kStoredPageHeaderSize);
  file->write(payload.data(), static_cast<std::streamsize>(payload.size()));
  if (!*file) return Status::IoError("pagefile wrapped append failed");
  if (bytes_written) *bytes_written = total_size;
  return Status::Ok();
}

Status PageFile::AppendPages(const std::vector<const uint8_t*>& pages,
                             uint64_t* first_offset_out) {
  if (pages.empty()) {
    if (first_offset_out) *first_offset_out = 0;
    return Status::Ok();
  }
  for (const uint8_t* page : pages) {
    if (!page) return Status::InvalidArgument("null page");
  }
  if (compress_pages_) wrapped_format_ = true;
  const uint64_t first_offset = next_offset_;
  std::fstream file(path_, std::ios::binary | std::ios::in | std::ios::out);
  if (!file) {
    std::ofstream create(path_, std::ios::binary);
    if (!create) return Status::IoError("pagefile create failed");
    PageFileHeader hdr{};
    create.write(reinterpret_cast<const char*>(&hdr), sizeof(hdr));
    std::vector<uint8_t> pad(kPageFileHeaderSize - sizeof(hdr), 0);
    create.write(reinterpret_cast<const char*>(pad.data()),
                 static_cast<std::streamsize>(pad.size()));
    create.close();
    file.open(path_, std::ios::binary | std::ios::in | std::ios::out);
  }
  if (!file) return Status::IoError("pagefile open failed");
  file.seekp(static_cast<std::streamoff>(first_offset));
  for (const uint8_t* page : pages) {
    uint64_t written = 0;
    const Status ws = WriteStoredPage(&file, page, kPageSize, &written);
    if (!ws.ok()) return ws;
    next_offset_ += written;
  }
  const Status hs = SaveHeader();
  if (!hs.ok()) return hs;
  if (first_offset_out) *first_offset_out = first_offset;
  return Status::Ok();
}

Status PageFile::AppendPage(const uint8_t* data, size_t len,
                            uint64_t* offset_out) {
  if (!data || len != kPageSize) {
    return Status::InvalidArgument("page must be 4096 bytes");
  }
  return AppendPages({data}, offset_out);
}

Status PageFile::EnsureReadFileOpen() const {
  if (read_file_.is_open()) return Status::Ok();
  read_file_.open(path_, std::ios::binary | std::ios::in | std::ios::out);
  if (!read_file_) {
    read_file_.clear();
    read_file_.open(path_, std::ios::binary | std::ios::in);
  }
  if (!read_file_) return Status::IoError("pagefile read open failed");
  ++read_opens_;
  return Status::Ok();
}

Status PageFile::ReadStoredPage(uint64_t offset,
                                std::vector<uint8_t>* logical_page) const {
  if (!logical_page) return Status::InvalidArgument("logical_page is null");
  logical_page->assign(kPageSize, 0);
  const Status open_st = EnsureReadFileOpen();
  if (!open_st.ok()) return open_st;

  if (!wrapped_format_) {
    read_file_.seekg(static_cast<std::streamoff>(offset));
    read_file_.read(reinterpret_cast<char*>(logical_page->data()),
                    static_cast<std::streamsize>(kPageSize));
    if (!read_file_) return Status::IoError("pagefile read failed");
    return Status::Ok();
  }

  char hdr[kStoredPageHeaderSize]{};
  read_file_.seekg(static_cast<std::streamoff>(offset));
  read_file_.read(hdr, kStoredPageHeaderSize);
  if (!read_file_) return Status::IoError("pagefile stored header read failed");
  const auto* u = reinterpret_cast<const unsigned char*>(hdr);
  const uint32_t total_size =
      static_cast<uint32_t>(u[0]) | (static_cast<uint32_t>(u[1]) << 8) |
      (static_cast<uint32_t>(u[2]) << 16) |
      (static_cast<uint32_t>(u[3]) << 24);
  const uint8_t codec = u[4];
  if (total_size < kStoredPageHeaderSize) {
    return Status::CorruptPage("stored page size invalid");
  }
  const size_t payload_size = total_size - kStoredPageHeaderSize;
  std::string payload(payload_size, '\0');
  read_file_.read(payload.data(), static_cast<std::streamsize>(payload_size));
  if (!read_file_) return Status::IoError("pagefile stored payload read failed");

  if (codec == kPageCodecRaw) {
    if (payload.size() != kPageSize) {
      return Status::CorruptPage("raw page size mismatch");
    }
    std::memcpy(logical_page->data(), payload.data(), kPageSize);
    return Status::Ok();
  }
  if (codec == kPageCodecLzma) {
    if (payload.size() < 4) return Status::CorruptPage("lzma page trunc");
    const auto* pu = reinterpret_cast<const unsigned char*>(payload.data());
    const uint32_t usize = static_cast<uint32_t>(pu[0]) |
                           (static_cast<uint32_t>(pu[1]) << 8) |
                           (static_cast<uint32_t>(pu[2]) << 16) |
                           (static_cast<uint32_t>(pu[3]) << 24);
    if (usize != kPageSize) return Status::CorruptPage("lzma page usize mismatch");
    std::string out;
    const Status ds = LzmaDecompressPayload(payload, usize, &out);
    if (!ds.ok()) return ds;
    std::memcpy(logical_page->data(), out.data(), kPageSize);
    return Status::Ok();
  }
  return Status::CorruptPage("unknown page codec");
}

Status PageFile::ReadPageUncached(uint64_t offset, uint8_t* data,
                                  size_t len) const {
  if (!data || len != kPageSize) {
    return Status::InvalidArgument("invalid read buffer");
  }
  std::vector<uint8_t> page;
  const Status rs = ReadStoredPage(offset, &page);
  if (!rs.ok()) return rs;
  std::memcpy(data, page.data(), kPageSize);
  return Status::Ok();
}

Status PageFile::ReadPage(uint64_t offset, uint8_t* data, size_t len) const {
  if (!data || len != kPageSize) {
    return Status::InvalidArgument("invalid read buffer");
  }
  if (cache_capacity_ > 0) {
    const auto it = cache_map_.find(offset);
    if (it != cache_map_.end()) {
      std::memcpy(data, cache_pages_.at(offset).data(), kPageSize);
      cache_lru_.splice(cache_lru_.begin(), cache_lru_, it->second);
      return Status::Ok();
    }
  }
  const Status rs = ReadPageUncached(offset, data, len);
  if (!rs.ok()) return rs;
  if (cache_capacity_ > 0) {
    if (cache_pages_.size() >= cache_capacity_) {
      const uint64_t evict = cache_lru_.back();
      cache_lru_.pop_back();
      cache_pages_.erase(evict);
      cache_map_.erase(evict);
    }
    cache_lru_.push_front(offset);
    cache_map_[offset] = cache_lru_.begin();
    cache_pages_[offset] =
        std::vector<uint8_t>(data, data + kPageSize);
  }
  return Status::Ok();
}

Status PageFile::ReadPages(const std::vector<uint64_t>& offsets,
                           std::vector<std::vector<uint8_t>>* pages_out) const {
  if (!pages_out) return Status::InvalidArgument("pages_out is null");
  pages_out->clear();
  if (offsets.empty()) return Status::Ok();

  pages_out->reserve(offsets.size());
  for (uint64_t offset : offsets) {
    if (cache_capacity_ > 0) {
      const auto it = cache_map_.find(offset);
      if (it != cache_map_.end()) {
        cache_lru_.splice(cache_lru_.begin(), cache_lru_, it->second);
        pages_out->push_back(cache_pages_.at(offset));
        continue;
      }
    }
    std::vector<uint8_t> page;
    const Status rs = ReadStoredPage(offset, &page);
    if (!rs.ok()) return rs;
    if (cache_capacity_ > 0) {
      if (cache_pages_.size() >= cache_capacity_) {
        const uint64_t evict = cache_lru_.back();
        cache_lru_.pop_back();
        cache_pages_.erase(evict);
        cache_map_.erase(evict);
      }
      cache_lru_.push_front(offset);
      cache_map_[offset] = cache_lru_.begin();
      cache_pages_[offset] = page;
    }
    pages_out->push_back(std::move(page));
  }
  return Status::Ok();
}

}  // namespace ebtree
