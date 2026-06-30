#pragma once

#include "rar_types.h"

#include "ebtree/common/status.h"

namespace ebtree {
namespace audit {

Status PhysicalAttest(const std::string& engine_path, uint32_t shard_count,
                      PhysicalReport* out);

}  // namespace audit
}  // namespace ebtree
