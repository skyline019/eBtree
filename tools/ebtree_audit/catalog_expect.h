#pragma once

#include <string>
#include <vector>

#include "ebtree/common/status.h"
#include "rar_types.h"

namespace ebtree {
namespace audit {

Status LoadCatalogExpectSnapshot(const std::string& catalog_path,
                                 ExpectSnapshot* out);

std::vector<std::string> CatalogTableIdPrefixes(const std::string& catalog_path);

}  // namespace audit
}  // namespace ebtree
