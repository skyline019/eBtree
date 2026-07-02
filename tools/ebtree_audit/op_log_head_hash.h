#pragma once

#include <string>

#include "ebtree/common/status.h"

namespace ebtree {
namespace audit {

Status OpLogHeadSha256(const std::string& op_log_path, std::string* out_hex);

}  // namespace audit
}  // namespace ebtree
