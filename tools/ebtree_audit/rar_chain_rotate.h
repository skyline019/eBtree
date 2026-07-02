#pragma once

#include <cstdint>
#include <string>

#include "ebtree/common/status.h"

namespace ebtree {
namespace audit {

Status RotateRarChainIfNeeded(const std::string& chain_path,
                              uint64_t max_entries = 10000);

uint64_t RarChainEntryCount(const std::string& chain_path);

}  // namespace audit
}  // namespace ebtree
