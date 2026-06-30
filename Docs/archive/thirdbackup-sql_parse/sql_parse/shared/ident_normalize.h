#pragma once

#include <string>

namespace heterodb::sql_parse {

/// Strip surrounding backticks from a token (no-op if absent).
std::string StripBackticks(std::string tok);

/// Normalize a SQL identifier token (backticks + trim).
std::string NormalizeIdent(const std::string& tok);

}  // namespace heterodb::sql_parse
