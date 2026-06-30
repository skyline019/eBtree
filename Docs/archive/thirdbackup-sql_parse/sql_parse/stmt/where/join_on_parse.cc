// ParseConcept | JOIN ON composite equality parsing.
#include "sql_parse/stmt/where/join_on_api.h"

#include "common/parse_error.h"

#include "sql_parse/shared/parse_shared.h"

namespace heterodb::sql_parse {
namespace detail {
namespace {

std::string BareJoinCol(const std::string& col) {
  const auto dot = col.find('.');
  return dot == std::string::npos ? col : col.substr(dot + 1);
}

Status ParseJoinOnEqPair(const std::vector<std::string>& tokens, size_t* pos,
                         std::string* left_col, std::string* right_col) {
  if (*pos + 2 >= tokens.size()) {
    return Status::Syntax("expected column = column in ON", ParseErrorKind::kSyntax);
  }
  const std::string left_raw = tokens[(*pos)++];
  if (*pos >= tokens.size() || tokens[*pos] != "=") {
    return Status::Syntax("expected = in ON", ParseErrorKind::kSyntax);
  }
  ++(*pos);
  if (*pos >= tokens.size()) {
    return Status::Syntax("expected column after = in ON", ParseErrorKind::kSyntax);
  }
  const std::string right_raw = tokens[(*pos)++];
  *left_col = BareJoinCol(left_raw);
  *right_col = BareJoinCol(right_raw);
  return Status::OK();
}

}  // namespace

Status ParseJoinOnCondition(const std::vector<std::string>& tokens, size_t* pos,
                            JoinClause* join) {
  if (*pos >= tokens.size() || Upper(tokens[*pos]) != "ON") {
    return Status::Syntax("expected ON", ParseErrorKind::kSyntax);
  }
  ++(*pos);
  join->on_eq_pairs.clear();
  Status s = ParseJoinOnEqPair(tokens, pos, &join->left_column, &join->right_column);
  if (!s.ok()) {
    return s;
  }
  join->on_eq_pairs.emplace_back(join->left_column, join->right_column);
  while (*pos < tokens.size() && Upper(tokens[*pos]) == "AND") {
    ++(*pos);
    std::string left_col;
    std::string right_col;
    s = ParseJoinOnEqPair(tokens, pos, &left_col, &right_col);
    if (!s.ok()) {
      return s;
    }
    join->on_eq_pairs.emplace_back(left_col, right_col);
  }
  return Status::OK();
}

}  // namespace detail
}  // namespace heterodb::sql_parse
