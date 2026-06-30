#pragma once

#include "rar_types.h"

#include "ebtree/common/status.h"

namespace ebtree {
namespace audit {

Status ContractAttest(const ExpectSnapshot& expect,
                      const RecoveryReport& recovery, ContractReport* out);

}  // namespace audit
}  // namespace ebtree
