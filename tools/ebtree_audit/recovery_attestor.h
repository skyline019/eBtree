#pragma once

#include "rar_types.h"

#include "ebtree/common/status.h"
#include "ebtree/engine/engine.h"

namespace ebtree {
namespace audit {

Status RecoveryAttest(Engine* engine, const std::vector<std::string>& keys,
                      bool any_badwal, RecoveryReport* out);

}  // namespace audit
}  // namespace ebtree
