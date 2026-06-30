#pragma once

#include <cstdint>
#include <string>

namespace ebtree {
namespace audit {

std::string Sha256Hex(const uint8_t* data, size_t len);
std::string Sha256HexString(const std::string& data);
std::string Sha256HexFile(const std::string& path);

}  // namespace audit
}  // namespace ebtree
