// ParseConcept | WHERE column predicates (pred::ParseWhere delegates here).
// Manifest: PredWhereClause
#include "sql_parse/stmt/parse_common_api.h"
#include "sql_parse/stmt/where/where_api.h"

#include "concept/query/parser.h"
#include "concept/query/expr.h"

#include <algorithm>
#include <cctype>
#include <sstream>
#include <unordered_map>
#include <vector>

#include "common/parse_error.h"
#include "concept/catalog/catalog_codec.h"
#include "concept/catalog/qualified_name.h"
#include "concept/catalog/value_codec.h"
#include "concept/schema/schema.h"
#include "sql_parse/shared/parse_shared.h"
#include "sql_parse/shared/subquery_attach.h"

namespace heterodb::sql_parse {
namespace detail {
Status ParseColumnPredicate(const std::vector<std::string>& tokens, size_t* pos,
                            KeyScanRange* range, SqlStatement* out) {
  if (*pos + 3 > tokens.size()) {
    return Status::Syntax("incomplete column predicate", ParseErrorKind::kSyntax);
  }
  if (tokens[*pos] == "(" && *pos + 1 < tokens.size() &&
      Upper(tokens[*pos + 1]) == "SELECT") {
    ++(*pos);
    const size_t start = *pos;
    int depth = 1;
    while (*pos < tokens.size() && depth > 0) {
      if (tokens[*pos] == "(") {
        ++depth;
      } else if (tokens[*pos] == ")") {
        --depth;
        if (depth == 0) {
          break;
        }
      }
      ++(*pos);
    }
    if (*pos >= tokens.size()) {
      return Status::Syntax("scalar subquery missing )", ParseErrorKind::kSyntax);
    }
    std::shared_ptr<SqlStatement> sub_stmt;
    const size_t sub_end = *pos;
    Status sub_st =
        AttachSelectSubqueryFromTokens(tokens, start, sub_end, &sub_stmt);
    if (!sub_st.ok()) {
      return sub_st;
    }
    ++(*pos);
    if (*pos >= tokens.size()) {
      return Status::Syntax("incomplete reverse scalar subquery", ParseErrorKind::kSyntax);
    }
    const std::string op_tok = tokens[(*pos)++];
    CompareOp op;
    Status os = ParseCompareOp(op_tok, &op);
    if (!os.ok()) {
      return os;
    }
    if (*pos >= tokens.size()) {
      return Status::Syntax("reverse scalar subquery needs column", ParseErrorKind::kSyntax);
    }
    std::string col = tokens[(*pos)++];
    const auto dot = col.find('.');
    if (dot != std::string::npos) {
      out->range_column = col;
    } else {
      out->range_column = col;
    }
    ColumnPredicate cp;
    cp.column = col;
    cp.op = op;
    cp.scalar_subquery = true;
    AttachSubqueryToPredicate(sub_stmt, &cp);
    MarkCorrelatedRefs(&cp, out->outer_table_names);
    out->where_and.push_back(std::move(cp));
    return Status::OK();
  }
  const std::string col = tokens[(*pos)++];
  out->range_column = col;
  if (*pos + 1 < tokens.size() && Upper(tokens[*pos]) == "IS") {
    ++(*pos);
    if (Upper(tokens[*pos]) == "NOT") {
      ++(*pos);
      if (*pos >= tokens.size() || Upper(tokens[*pos]) != "NULL") {
        return Status::Syntax("expected NULL after IS NOT", ParseErrorKind::kSyntax);
      }
      ++(*pos);
      ColumnPredicate cp;
      cp.column = col;
      cp.op = CompareOp::kIsNotNull;
      out->where_and.push_back(cp);
      return Status::OK();
    }
    if (*pos < tokens.size() && Upper(tokens[*pos]) == "UNKNOWN") {
      ++(*pos);
      ColumnPredicate cp;
      cp.column = col;
      cp.op = CompareOp::kIsUnknown;
      out->where_and.push_back(cp);
      return Status::OK();
    }
    if (*pos >= tokens.size() || Upper(tokens[*pos]) != "NULL") {
      return Status::Syntax("expected NULL after IS", ParseErrorKind::kSyntax);
    }
    ++(*pos);
    ColumnPredicate cp;
    cp.column = col;
    cp.op = CompareOp::kIsNull;
    out->where_and.push_back(cp);
    return Status::OK();
  }
  if (*pos < tokens.size() && Upper(tokens[*pos]) == "BETWEEN") {
    ++(*pos);
    if (*pos + 2 >= tokens.size() || Upper(tokens[*pos + 1]) != "AND") {
      return Status::Syntax("BETWEEN requires low AND high", ParseErrorKind::kSyntax);
    }
    const std::string low = Unquote(tokens[(*pos)++]);
    ++(*pos);
    const std::string high = Unquote(tokens[(*pos)++]);
    ApplyKeyBound(range, CompareOp::kGe, low);
    ApplyKeyBound(range, CompareOp::kLe, high);
    ColumnPredicate cp;
    cp.column = col;
    cp.op = CompareOp::kGe;
    cp.value = low;
    cp.value2 = high;
    out->where_and.push_back(cp);
    return Status::OK();
  }
  const bool not_in =
      *pos < tokens.size() && Upper(tokens[*pos]) == "NOT" &&
      *pos + 1 < tokens.size() && Upper(tokens[*pos + 1]) == "IN";
  if (not_in || (*pos < tokens.size() && Upper(tokens[*pos]) == "IN")) {
    if (not_in) {
      *pos += 2;
    } else {
      ++(*pos);
    }
    if (*pos >= tokens.size() || tokens[*pos] != "(") {
      return Status::Syntax(not_in ? "NOT IN requires ( value list )"
                                   : "IN requires ( value list )",
                            ParseErrorKind::kSyntax);
    }
    ++(*pos);
    if (*pos < tokens.size() && Upper(tokens[*pos]) == "SELECT") {
      const size_t start = *pos;
      int depth = 1;
      while (*pos < tokens.size() && depth > 0) {
        if (tokens[*pos] == "(") {
          ++depth;
        } else if (tokens[*pos] == ")") {
          --depth;
          if (depth == 0) {
            break;
          }
        }
        ++(*pos);
      }
      if (*pos >= tokens.size()) {
        return Status::Syntax(not_in ? "NOT IN subquery missing )"
                                   : "IN subquery missing )",
                              ParseErrorKind::kSyntax);
      }
      ColumnPredicate cp;
      cp.column = col;
      cp.op = not_in ? CompareOp::kNotInSubquery : CompareOp::kInSubquery;
      std::shared_ptr<SqlStatement> sub_stmt;
      const size_t sub_end = *pos;
      Status sub_st =
          AttachSelectSubqueryFromTokens(tokens, start, sub_end, &sub_stmt);
      if (!sub_st.ok()) {
        return sub_st;
      }
      ++(*pos);
      AttachSubqueryToPredicate(sub_stmt, &cp);
      MarkCorrelatedRefs(&cp, out->outer_table_names);
      out->where_and.push_back(std::move(cp));
      return Status::OK();
    }
    ColumnPredicate cp;
    cp.column = col;
    cp.op = not_in ? CompareOp::kNotIn : CompareOp::kIn;
    while (*pos < tokens.size() && tokens[*pos] != ")") {
      if (tokens[*pos] == ",") {
        ++(*pos);
        continue;
      }
      cp.in_values.push_back(Unquote(tokens[(*pos)++]));
    }
    if (*pos >= tokens.size() || tokens[*pos] != ")") {
      return Status::Syntax(not_in ? "NOT IN list missing )" : "IN list missing )", ParseErrorKind::kSyntax);
    }
    ++(*pos);
    out->where_and.push_back(cp);
    return Status::OK();
  }
  if (*pos + 1 < tokens.size() && Upper(tokens[*pos]) == "NOT" &&
      Upper(tokens[*pos + 1]) == "LIKE") {
    *pos += 2;
    if (*pos >= tokens.size()) {
      return Status::Syntax("NOT LIKE requires pattern", ParseErrorKind::kSyntax);
    }
    ColumnPredicate cp;
    cp.column = col;
    cp.op = CompareOp::kNotLike;
    cp.value = Unquote(tokens[(*pos)++]);
    if (*pos + 1 < tokens.size() && Upper(tokens[*pos]) == "ESCAPE") {
      ++(*pos);
      if (*pos >= tokens.size()) {
        return Status::Syntax("ESCAPE requires character", ParseErrorKind::kSyntax);
      }
      cp.like_escape = Unquote(tokens[(*pos)++]);
      if (cp.like_escape.empty()) {
        cp.like_escape = "\\";
      }
    }
    out->where_and.push_back(cp);
    return Status::OK();
  }
  if (*pos < tokens.size() &&
      (Upper(tokens[*pos]) == "REGEXP" || Upper(tokens[*pos]) == "RLIKE")) {
    ++(*pos);
    if (*pos >= tokens.size()) {
      return Status::Syntax("REGEXP requires pattern", ParseErrorKind::kSyntax);
    }
    ColumnPredicate cp;
    cp.column = col;
    cp.op = CompareOp::kRegexp;
    cp.value = Unquote(tokens[(*pos)++]);
    out->where_and.push_back(cp);
    return Status::OK();
  }
  if (*pos < tokens.size() && Upper(tokens[*pos]) == "LIKE") {
    ++(*pos);
    if (*pos >= tokens.size()) {
      return Status::Syntax("LIKE requires pattern", ParseErrorKind::kSyntax);
    }
    ColumnPredicate cp;
    cp.column = col;
    cp.op = CompareOp::kLike;
    cp.value = Unquote(tokens[(*pos)++]);
    if (*pos + 1 < tokens.size() && Upper(tokens[*pos]) == "ESCAPE") {
      ++(*pos);
      if (*pos >= tokens.size()) {
        return Status::Syntax("ESCAPE requires character", ParseErrorKind::kSyntax);
      }
      cp.like_escape = Unquote(tokens[(*pos)++]);
      if (cp.like_escape.empty()) {
        cp.like_escape = "\\";
      }
    }
    out->where_and.push_back(cp);
    return Status::OK();
  }
  const std::string op_tok = tokens[(*pos)++];
  CompareOp op;
  Status s = ParseCompareOp(op_tok, &op);
  if (!s.ok()) {
    return s;
  }
  const bool is_any =
      *pos < tokens.size() && Upper(tokens[*pos]) == "ANY";
  const bool is_all =
      *pos < tokens.size() && Upper(tokens[*pos]) == "ALL";
  if (is_any || is_all) {
    ++(*pos);
    if (*pos >= tokens.size() || tokens[*pos] != "(") {
      return Status::Syntax(is_all ? "ALL requires ( subquery )"
                                   : "ANY requires ( subquery )",
                            ParseErrorKind::kSyntax);
    }
    ++(*pos);
    if (*pos >= tokens.size() || Upper(tokens[*pos]) != "SELECT") {
      return Status::Syntax(is_all ? "ALL requires ( SELECT … )"
                                   : "ANY requires ( SELECT … )",
                            ParseErrorKind::kSyntax);
    }
    const size_t start = *pos;
    int depth = 1;
    while (*pos < tokens.size() && depth > 0) {
      if (tokens[*pos] == "(") {
        ++depth;
      } else if (tokens[*pos] == ")") {
        --depth;
        if (depth == 0) {
          break;
        }
      }
      ++(*pos);
    }
    if (*pos >= tokens.size()) {
      return Status::Syntax(is_all ? "ALL subquery missing )"
                                   : "ANY subquery missing )",
                            ParseErrorKind::kSyntax);
    }
    ColumnPredicate cp;
    cp.column = col;
    cp.op = is_all ? CompareOp::kAllSubquery : CompareOp::kAnySubquery;
    cp.quantifier_cmp = op;
    std::shared_ptr<SqlStatement> sub_stmt;
    const size_t sub_end = *pos;
    Status sub_st =
        AttachSelectSubqueryFromTokens(tokens, start, sub_end, &sub_stmt);
    if (!sub_st.ok()) {
      return sub_st;
    }
    ++(*pos);
    AttachSubqueryToPredicate(sub_stmt, &cp);
    MarkCorrelatedRefs(&cp, out->outer_table_names);
    out->where_and.push_back(std::move(cp));
    return Status::OK();
  }
  if (*pos < tokens.size() && tokens[*pos] == "(" &&
      *pos + 1 < tokens.size() && Upper(tokens[*pos + 1]) == "SELECT") {
    ++(*pos);
    const size_t start = *pos;
    int depth = 1;
    while (*pos < tokens.size() && depth > 0) {
      if (tokens[*pos] == "(") {
        ++depth;
      } else if (tokens[*pos] == ")") {
        --depth;
        if (depth == 0) {
          break;
        }
      }
      ++(*pos);
    }
    if (*pos >= tokens.size()) {
      return Status::Syntax("scalar subquery missing )", ParseErrorKind::kSyntax);
    }
    ColumnPredicate cp;
    cp.column = col;
    cp.op = op;
    cp.scalar_subquery = true;
    std::shared_ptr<SqlStatement> sub_stmt;
    const size_t sub_end = *pos;
    Status sub_st =
        AttachSelectSubqueryFromTokens(tokens, start, sub_end, &sub_stmt);
    if (!sub_st.ok()) {
      return sub_st;
    }
    ++(*pos);
    AttachSubqueryToPredicate(sub_stmt, &cp);
    MarkCorrelatedRefs(&cp, out->outer_table_names);
    out->where_and.push_back(std::move(cp));
    return Status::OK();
  }
  const std::string val = Unquote(tokens[(*pos)++]);
  ApplyKeyBound(range, op, val);
  ColumnPredicate cp;
  cp.column = col;
  cp.op = op;
  cp.value = val;
  out->where_and.push_back(cp);
  if (op == CompareOp::kEq) {
    out->where = Predicate{};
    out->where->column = col;
    out->where->op = CompareOp::kEq;
    out->where->value = val;
  }
  return Status::OK();
}
Status ParseWhereExpr(const std::vector<std::string>& tokens, size_t* pos,
                      KeyScanRange* range, SqlStatement* out);
Status ParseKeyPredicate(const std::vector<std::string>& tokens, size_t* pos,
                         KeyScanRange* range, SqlStatement* out) {
  out->range_column = "key";
  if (*pos + 3 > tokens.size()) {
    return Status::Syntax("incomplete key predicate", ParseErrorKind::kSyntax);
  }
  const std::string col = tokens[(*pos)++];
  if (col != "key") {
    return Status::Syntax("hetero_kv range WHERE only supports key column", ParseErrorKind::kSyntax);
  }
  if (*pos < tokens.size() && Upper(tokens[*pos]) == "BETWEEN") {
    ++(*pos);
    if (*pos + 2 >= tokens.size() || Upper(tokens[*pos + 1]) != "AND") {
      return Status::Syntax("BETWEEN requires low AND high", ParseErrorKind::kSyntax);
    }
    const std::string low = Unquote(tokens[(*pos)++]);
    ++(*pos);
    const std::string high = Unquote(tokens[(*pos)++]);
    ApplyKeyBound(range, CompareOp::kGe, low);
    ApplyKeyBound(range, CompareOp::kLe, high);
    return Status::OK();
  }
  const std::string op_tok = tokens[(*pos)++];
  CompareOp op;
  Status s = ParseCompareOp(op_tok, &op);
  if (!s.ok()) {
    return s;
  }
  const std::string val = Unquote(tokens[(*pos)++]);
  ApplyKeyBound(range, op, val);
  if (op == CompareOp::kEq) {
    out->where = Predicate{};
    out->where->column = "key";
    out->where->op = CompareOp::kEq;
    out->where->value = range->start;
  }
  return Status::OK();
}

bool IsWhereStopToken(const std::string& tok) {
  const std::string u = Upper(tok);
  return u == "GROUP" || u == "ORDER" || u == "LIMIT" || u == "HAVING" ||
         u == "JOIN" || u == ";";
}
Status ParseWherePrimary(const std::vector<std::string>& tokens, size_t* pos,
                         KeyScanRange* range,
                         std::vector<ColumnPredicate>* clause,
                         SqlStatement* scratch) {
  if (*pos >= tokens.size()) {
    return Status::Syntax("incomplete WHERE", ParseErrorKind::kSyntax);
  }
  auto parse_exists = [&](bool negate) -> Status {
    if (*pos >= tokens.size() || tokens[*pos] != "(") {
      return Status::Syntax("EXISTS requires ( SELECT ... )", ParseErrorKind::kSyntax);
    }
    ++(*pos);
    if (*pos >= tokens.size() || Upper(tokens[*pos]) != "SELECT") {
      return Status::Syntax("EXISTS requires ( SELECT ... )", ParseErrorKind::kSyntax);
    }
    const size_t start = *pos;
    int depth = 1;
    while (*pos < tokens.size() && depth > 0) {
      if (tokens[*pos] == "(") {
        ++depth;
      } else if (tokens[*pos] == ")") {
        --depth;
        if (depth == 0) {
          break;
        }
      }
      ++(*pos);
    }
    if (*pos >= tokens.size()) {
      return Status::Syntax("EXISTS subquery missing )", ParseErrorKind::kSyntax);
    }
    std::shared_ptr<SqlStatement> sub_stmt;
    const size_t sub_end = *pos;
    Status sub_st =
        AttachSelectSubqueryFromTokens(tokens, start, sub_end, &sub_stmt);
    if (!sub_st.ok()) {
      return sub_st;
    }
    ++(*pos);
    ColumnPredicate cp;
    cp.op = negate ? CompareOp::kNotExists : CompareOp::kExists;
    AttachSubqueryToPredicate(sub_stmt, &cp);
    MarkCorrelatedRefs(&cp, scratch->outer_table_names);
    clause->push_back(std::move(cp));
    return Status::OK();
  };
  if (Upper(tokens[*pos]) == "NOT") {
    if (*pos + 1 < tokens.size() && Upper(tokens[*pos + 1]) == "EXISTS") {
      *pos += 2;
      return parse_exists(true);
    }
  }
  if (Upper(tokens[*pos]) == "EXISTS") {
    ++(*pos);
    return parse_exists(false);
  }
  if (tokens[*pos] == "(") {
    ++(*pos);
    Status s = ParseWhereExpr(tokens, pos, range, scratch);
    if (!s.ok()) {
      return s;
    }
    if (*pos >= tokens.size() || tokens[*pos] != ")") {
      return Status::Syntax("expected )", ParseErrorKind::kSyntax);
    }
    ++(*pos);
    if (!scratch->where_or.empty()) {
      if (scratch->where_or.size() > 1) {
        scratch->where_or.clear();
        return Status::Syntax("parenthesized OR group", ParseErrorKind::kSyntax);
      }
      for (const auto& c : scratch->where_or) {
        clause->insert(clause->end(), c.begin(), c.end());
      }
      scratch->where_or.clear();
    } else if (!scratch->where_and.empty()) {
      clause->insert(clause->end(), scratch->where_and.begin(),
                     scratch->where_and.end());
      scratch->where_and.clear();
    }
    return Status::OK();
  }
  const size_t base = scratch->where_and.size();
  Status s = ParseColumnPredicate(tokens, pos, range, scratch);
  if (!s.ok()) {
    return s;
  }
  clause->insert(clause->end(),
                 scratch->where_and.begin() + static_cast<std::ptrdiff_t>(base),
                 scratch->where_and.end());
  scratch->where_and.resize(base);
  return Status::OK();
}

Status ParseWhereAndTerm(const std::vector<std::string>& tokens, size_t* pos,
                         KeyScanRange* range, std::vector<ColumnPredicate>* clause,
                         SqlStatement* scratch) {
  Status s = ParseWherePrimary(tokens, pos, range, clause, scratch);
  if (!s.ok()) {
    return s;
  }
  while (*pos < tokens.size() && Upper(tokens[*pos]) == "AND" &&
         !IsWhereStopToken(tokens[*pos])) {
    ++(*pos);
    s = ParseWherePrimary(tokens, pos, range, clause, scratch);
    if (!s.ok()) {
      return s;
    }
  }
  return Status::OK();
}

Status ParseWhereExpr(const std::vector<std::string>& tokens, size_t* pos,
                      KeyScanRange* range, SqlStatement* out) {
  SqlStatement scratch;
  scratch.outer_table_names = out->outer_table_names;
  std::vector<ColumnPredicate> clause;
  Status s = ParseWhereAndTerm(tokens, pos, range, &clause, &scratch);
  if (!s.ok()) {
    return s;
  }
  if (!clause.empty()) {
    out->where_or.push_back(clause);
  }
  while (*pos < tokens.size() && Upper(tokens[*pos]) == "OR" &&
         !IsWhereStopToken(tokens[*pos])) {
    ++(*pos);
    clause.clear();
    s = ParseWhereAndTerm(tokens, pos, range, &clause, &scratch);
    if (!s.ok()) {
      return s;
    }
    if (!clause.empty()) {
      out->where_or.push_back(clause);
    }
  }
  if (out->where_or.size() == 1) {
    out->where_and = out->where_or[0];
    out->where_or.clear();
  }
  return Status::OK();
}

Status ParseWhereClause(const std::vector<std::string>& tokens, size_t* pos,
                        SqlStatement* out) {
  if (*pos >= tokens.size() || Upper(tokens[*pos]) != "WHERE") {
    return Status::Syntax("expected WHERE", ParseErrorKind::kSyntax);
  }
  ++(*pos);
  KeyScanRange range;
  range.start.clear();
  range.end_exclusive = "\xFF";
  out->where_or.clear();
  out->where_and.clear();
  out->where.reset();
  out->where_expr.reset();
  Status s = ParseWhereExpr(tokens, pos, &range, out);
  if (!s.ok()) {
    return s;
  }
  SyncKeyScanRange(out);
  if (!range.start.empty() && range.end_exclusive == range.start + '\x00' &&
      out->where_and.size() == 1 && out->where_and[0].op == CompareOp::kEq &&
      BareColumnToken(out->where_and[0].column) == "key") {
    out->where = Predicate{};
    out->where->column = out->where_and[0].column;
    out->where->op = CompareOp::kEq;
    out->where->value = range.start.empty() ? out->where_and[0].value : range.start;
  }
  return Status::OK();
}

}  // namespace detail
}  // namespace heterodb::sql_parse
