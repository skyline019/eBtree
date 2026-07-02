#include "index_match.h"

namespace ebtree {
namespace sql {
namespace plan {

namespace {

std::string BareColumn(const ExprNode& node) {
  const auto dot = node.column.find('.');
  return dot == std::string::npos ? node.column : node.column.substr(dot + 1);
}

bool ExtractColLitEq(const ExprNode* node, std::string* col, std::string* lit) {
  if (!node || node->kind != ExprKind::kBinary || node->bin_op != BinaryOp::kEq ||
      node->children.size() != 2) {
    return false;
  }
  if (node->children[0]->kind != ExprKind::kColumn ||
      node->children[1]->kind != ExprKind::kLiteral) {
    return false;
  }
  *col = BareColumn(*node->children[0]);
  *lit = node->children[1]->literal;
  return true;
}

bool ExtractColLitCmp(const ExprNode* node, BinaryOp op, std::string* col,
                      std::string* lit) {
  if (!node || node->kind != ExprKind::kBinary || node->bin_op != op ||
      node->children.size() != 2) {
    return false;
  }
  if (node->children[0]->kind != ExprKind::kColumn ||
      node->children[1]->kind != ExprKind::kLiteral) {
    return false;
  }
  *col = BareColumn(*node->children[0]);
  *lit = node->children[1]->literal;
  return true;
}

bool TryRangeAnd(const ExprNode* where, std::string* col, std::string* lo,
                 std::string* hi) {
  if (!where || where->kind != ExprKind::kBinary || where->bin_op != BinaryOp::kAnd ||
      where->children.size() != 2) {
    return false;
  }
  const ExprNode* a = where->children[0].get();
  const ExprNode* b = where->children[1].get();
  std::string col_a;
  std::string col_b;
  std::string lit_ge;
  std::string lit_le;
  bool has_ge = false;
  bool has_le = false;
  if (ExtractColLitCmp(a, BinaryOp::kGe, &col_a, &lit_ge)) {
    has_ge = true;
  } else if (ExtractColLitCmp(a, BinaryOp::kGt, &col_a, &lit_ge)) {
    has_ge = true;
  }
  if (ExtractColLitCmp(b, BinaryOp::kLe, &col_b, &lit_le)) {
    has_le = true;
  } else if (ExtractColLitCmp(b, BinaryOp::kLt, &col_b, &lit_le)) {
    has_le = true;
  }
  if (!has_ge) {
    if (ExtractColLitCmp(a, BinaryOp::kLe, &col_a, &lit_le)) has_le = true;
    if (ExtractColLitCmp(a, BinaryOp::kGe, &col_a, &lit_ge)) has_ge = true;
  }
  if (!has_le) {
    if (ExtractColLitCmp(b, BinaryOp::kGe, &col_b, &lit_ge)) has_ge = true;
    if (ExtractColLitCmp(b, BinaryOp::kLe, &col_b, &lit_le)) has_le = true;
  }
  if (!has_ge || !has_le || col_a != col_b) return false;
  *col = col_a;
  *lo = lit_ge;
  *hi = lit_le;
  return true;
}

}  // namespace

const IndexDef* FindLeadingIndex(const Catalog* catalog,
                                 const std::string& table_name,
                                 const std::string& column) {
  if (!catalog) return nullptr;
  for (const auto& idx : catalog->IndexesForTable(table_name)) {
    if (!idx.columns.empty() && idx.columns[0] == column) {
      return &idx;
    }
  }
  return nullptr;
}

IndexMatch MatchIndexWhere(const ExprNode* where, const Catalog* catalog,
                           const std::string& table_name) {
  IndexMatch m;
  if (!where || !catalog) return m;

  std::string col;
  std::string lit;
  if (ExtractColLitEq(where, &col, &lit)) {
    if (FindLeadingIndex(catalog, table_name, col)) {
      m.mode = IndexScanMode::kEq;
      m.index_column = col;
      m.eq_value = lit;
    }
    return m;
  }

  std::string lo;
  std::string hi;
  if (TryRangeAnd(where, &col, &lo, &hi)) {
    if (FindLeadingIndex(catalog, table_name, col)) {
      m.mode = IndexScanMode::kRange;
      m.index_column = col;
      m.range_lo = lo;
      m.range_hi = hi;
    }
  }
  return m;
}

}  // namespace plan
}  // namespace sql
}  // namespace ebtree
