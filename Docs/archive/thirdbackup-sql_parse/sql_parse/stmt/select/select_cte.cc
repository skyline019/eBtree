// ParseConcept | SELECT — WITH / CTE parsing.
// Manifest: StmtRule_With
#include "sql_parse/stmt/select/select_api.h"

#include "common/parse_error.h"
#include "sql_parse/stmt/dml/dml_api.h"
#include "sql_parse/stmt/parse_statement.h"
#include "sql_parse/stmt/where/where_api.h"

#include "concept/query/parser.h"
#include "concept/query/expr.h"

#include <sstream>

#include "sql_parse/shared/parse_shared.h"

namespace heterodb::sql_parse {
namespace detail {
Status ParseOneCte(const std::vector<std::string>& tokens, size_t* pos,
                  SqlStatement* out) {
  if (*pos >= tokens.size()) {
    return Status::Syntax("WITH requires CTE name", ParseErrorKind::kSyntax);
  }
  std::string cte_name = tokens[(*pos)++];
  if (*pos >= tokens.size() || Upper(tokens[*pos]) != "AS") {
    return Status::Syntax("WITH name AS expected", ParseErrorKind::kSyntax);
  }
  ++(*pos);
  if (*pos >= tokens.size() || tokens[*pos] != "(") {
    return Status::Syntax("WITH AS ( subquery ) expected", ParseErrorKind::kSyntax);
  }
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
  std::ostringstream subsql;
  for (size_t i = start; i < *pos; ++i) {
    if (i > start) {
      subsql << ' ';
    }
    subsql << tokens[i];
  }
  ++(*pos);
  SqlStatement cte_query;
  Status s = ParseSqlStatement(subsql.str(), &cte_query);
  if (!s.ok()) {
    return s;
  }
  if (cte_query.kind != SqlStatementKind::kSelect) {
    return Status::Syntax("CTE body must be SELECT", ParseErrorKind::kSyntax);
  }
  out->cte_names.push_back(std::move(cte_name));
  out->cte_queries.push_back(std::move(cte_query));
  return Status::OK();
}

Status ParseWithClause(const std::vector<std::string>& tokens, size_t* pos,
                       SqlStatement* out) {
  out->with_recursive = false;
  out->recursive_cte = {};
  if (*pos < tokens.size() && Upper(tokens[*pos]) == "RECURSIVE") {
    ++(*pos);
    Status s = ParseRecursiveCte(tokens, pos, out);
    if (!s.ok()) {
      return s;
    }
    while (*pos < tokens.size() && tokens[*pos] == ",") {
      ++(*pos);
      s = ParseOneCte(tokens, pos, out);
      if (!s.ok()) {
        return s;
      }
    }
  } else {
    do {
      Status s = ParseOneCte(tokens, pos, out);
      if (!s.ok()) {
        return s;
      }
      if (*pos < tokens.size() && tokens[*pos] == ",") {
        ++(*pos);
        continue;
      }
      break;
    } while (true);
  }
  if (*pos >= tokens.size()) {
    return Status::Syntax("expected statement after WITH clause", ParseErrorKind::kSyntax);
  }
  const std::string head = Upper(tokens[*pos]);
  if (head == "SELECT") {
    return ParseQueryExpression(tokens, pos, out);
  }
  if (head == "INSERT" || head == "UPDATE" || head == "DELETE") {
    ++(*pos);
    SqlStatement dml;
    Status s = ParseStmtDml(head, tokens, pos, &dml);
    if (!s.ok()) {
      return s;
    }
    dml.cte_names = std::move(out->cte_names);
    dml.cte_queries = std::move(out->cte_queries);
    dml.with_recursive = out->with_recursive;
    dml.recursive_cte = out->recursive_cte;
    *out = std::move(dml);
    return Status::OK();
  }
  return Status::Syntax("expected SELECT or DML after WITH clause", ParseErrorKind::kSyntax);
}

}  // namespace detail
}  // namespace heterodb::sql_parse
