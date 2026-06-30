#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

namespace ebtree {

std::string Sha256Hex(const uint8_t* data, size_t len);
std::string Sha256HexString(const std::string& data);

}  // namespace ebtree
