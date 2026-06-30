#pragma once

#include "rar_types.h"

namespace ebtree {
namespace audit {

RarVerdict ApplyPolicyGate(const RarReport& report, std::string* reason_out);

}  // namespace audit
}  // namespace ebtree
