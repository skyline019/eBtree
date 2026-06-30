#pragma once

#include <string>
#include <vector>

namespace heterodb::sql_parse {

std::string Trim(std::string s);
std::string Upper(std::string s);
bool SqlInputNeedsNormalization(const std::string& sql);
std::string NormalizeSqlInput(std::string sql);
std::vector<std::string> SplitTokens(const std::string& sql);
std::vector<std::string> SplitSqlStatements(const std::string& sql);

}  // namespace heterodb::sql_parse
