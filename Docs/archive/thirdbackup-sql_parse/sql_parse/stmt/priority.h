#pragma once

namespace heterodb::sql_parse::stmt_priority {

constexpr int kLegacyFallback = 0;
constexpr int kKeywordAlias = 100;
constexpr int kStatement = 500;
constexpr int kSyntaxPrefix = 1000;

}  // namespace heterodb::sql_parse::stmt_priority
