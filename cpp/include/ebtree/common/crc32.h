#pragma once

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>

namespace ebtree {

inline uint32_t Crc32(const void* data, size_t len) {
  const auto* p = static_cast<const uint8_t*>(data);
  uint32_t crc = 0xFFFFFFFFu;
  for (size_t i = 0; i < len; ++i) {
    crc ^= p[i];
    for (int b = 0; b < 8; ++b) {
      if (crc & 1u) {
        crc = (crc >> 1) ^ 0xEDB88320u;
      } else {
        crc >>= 1;
      }
    }
  }
  return ~crc;
}

inline std::string HexU64(uint64_t v) {
  char buf[17];
  snprintf(buf, sizeof(buf), "%016llx",
           static_cast<unsigned long long>(v));
  return std::string(buf);
}

}  // namespace ebtree
