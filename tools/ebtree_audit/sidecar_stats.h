#pragma once

#include <cstdint>
#include <string>

#include "ebtree/common/status.h"
#include "rar_types.h"

namespace ebtree {
namespace audit {

Status LoadExpectFromJson(const std::string& path, ContractMode mode,
                          ExpectSnapshot* out);

Status CollectOpLogSidecarStats(const std::string& path,
                                OpLogSidecarReport* out);

Status CollectCatalogSidecarStats(const std::string& path,
                                  CatalogSidecarReport* out);

}  // namespace audit
}  // namespace ebtree
