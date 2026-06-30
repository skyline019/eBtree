#include "ebtree/common/status.h"

namespace ebtree {

Status::Status(StatusCode code, std::string message)
    : code_(code), message_(std::move(message)) {}

}  // namespace ebtree
