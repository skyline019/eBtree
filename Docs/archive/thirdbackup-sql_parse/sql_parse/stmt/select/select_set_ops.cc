// ParseConcept | SELECT — UNION / INTERSECT / EXCEPT chain.
#include "sql_parse/stmt/select/select_api.h"

#include "common/parse_error.h"
#include "sql_parse/shared/parse_shared.h"

namespace heterodb::sql_parse {
namespace detail {
Status ParseSetOpKindToken(const std::string& tok, SetOpKind* kind) {
  const std::string u = Upper(tok);
  if (u == "UNION") {
    *kind = SetOpKind::kUnion;
    return Status::OK();
  }
  if (u == "INTERSECT") {
    *kind = SetOpKind::kIntersect;
    return Status::OK();
  }
  if (u == "EXCEPT") {
    *kind = SetOpKind::kExcept;
    return Status::OK();
  }
  return Status::Syntax("expected UNION, INTERSECT, or EXCEPT", ParseErrorKind::kSyntax);
}

bool IsSetOpLeadToken(const std::string& tok) {
  const std::string u = Upper(tok);
  return u == "UNION" || u == "INTERSECT" || u == "EXCEPT";
}

Status ParseQueryExpression(const std::vector<std::string>& tokens, size_t* pos,
                          SqlStatement* out) {
  out->union_branches.clear();
  out->set_op_chain_ops.clear();
  out->set_op_chain_all.clear();
  out->set_op_chain_branches.clear();
  out->union_all = true;
  out->set_op_kind = SetOpKind::kUnion;
  Status s = ParseSelectCore(tokens, pos, out);
  if (!s.ok()) {
    return s;
  }
  const size_t ncol = out->columns.size();
  for (size_t scan = *pos; scan < tokens.size(); ++scan) {
    if (!IsSetOpLeadToken(tokens[scan])) {
      continue;
    }
    if (*pos < tokens.size() &&
        (Upper(tokens[*pos]) == "ORDER" || Upper(tokens[*pos]) == "LIMIT" ||
         Upper(tokens[*pos]) == "OFFSET")) {
      return Status::Syntax(
          "ORDER BY/LIMIT/OFFSET not allowed before set operation",
          ParseErrorKind::kSyntax);
    }
    break;
  }
  std::vector<SetOpKind> ops;
  std::vector<bool> alls;
  std::vector<SqlStatement> branches;
  while (*pos < tokens.size() && IsSetOpLeadToken(tokens[*pos])) {
    SetOpKind op = SetOpKind::kUnion;
    s = ParseSetOpKindToken(tokens[*pos], &op);
    if (!s.ok()) {
      return s;
    }
    ++(*pos);
    bool all = true;
    if (*pos < tokens.size() && Upper(tokens[*pos]) == "ALL") {
      ++(*pos);
    } else {
      all = false;
    }
    SqlStatement branch;
    s = ParseSelectCore(tokens, pos, &branch);
    if (!s.ok()) {
      return s;
    }
    if (branch.columns.size() != ncol) {
      return Status::Syntax("set operation branches must have same column count", ParseErrorKind::kSyntax);
    }
    ops.push_back(op);
    alls.push_back(all);
    branches.push_back(std::move(branch));
  }
  if (!ops.empty()) {
    bool homogeneous = true;
    for (size_t i = 1; i < ops.size(); ++i) {
      if (ops[i] != ops[0] || alls[i] != alls[0]) {
        homogeneous = false;
        break;
      }
    }
    if (homogeneous) {
      out->set_op_kind = ops.front();
      out->union_all = alls.front();
      out->union_branches = std::move(branches);
    } else {
      out->set_op_chain_ops = std::move(ops);
      out->set_op_chain_all = std::move(alls);
      out->set_op_chain_branches = std::move(branches);
    }
  }
  Status ts = ParseSelectTail(tokens, pos, out);
  if (!ts.ok()) {
    return ts;
  }
  return Status::OK();
}

}  // namespace detail
}  // namespace heterodb::sql_parse
