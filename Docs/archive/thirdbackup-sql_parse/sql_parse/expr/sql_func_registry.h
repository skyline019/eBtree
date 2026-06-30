#pragma once

#include <string_view>
#include <vector>

namespace heterodb::sql_parse {

std::vector<std::string_view> SqlParseFunctionNames();

bool IsSqlParseSupportedFunction(std::string_view upper_name);

bool SqlFunctionEvalSupported(std::string_view upper_name);

bool SqlFunctionIsAggregate(std::string_view upper_name);

}  // namespace heterodb::sql_parse
