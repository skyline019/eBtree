#pragma once

#include <string>
#include <vector>

#include "ebtree/common/status.h"
#include "rar_types.h"

namespace ebtree {
namespace audit {

Status LoadOpLogExpectSnapshot(const std::string& path,
                               audit::ContractMode mode,
                               ExpectSnapshot* out);

std::vector<std::string> CollectProbeKeysFromExpect(
    const ExpectSnapshot& expect);

}  // namespace audit
}  // namespace ebtree
