#include "ebtree/common/status.h"

namespace ebtree {
namespace sql {

Status FeatureNotSupported(const char* feature) {
  return Status::InvalidArgument(std::string("SQLFeatureNotSupported: ") + feature);
}

}  // namespace sql
}  // namespace ebtree
