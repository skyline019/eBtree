#pragma once

#include "rar_types.h"

#include <ostream>
#include <string>

namespace ebtree {
namespace audit {

void WriteRarReportJson(const RarReport& report, std::ostream& out);
std::string RarReportToJson(const RarReport& report);

}  // namespace audit
}  // namespace ebtree
