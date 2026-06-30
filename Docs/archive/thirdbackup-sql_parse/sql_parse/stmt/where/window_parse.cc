// ParseConcept | WINDOW OVER / frame parsing.
#include "sql_parse/stmt/parse_common_api.h"

#include "common/parse_error.h"
#include "sql_parse/stmt/where/where_api.h"

#include "concept/query/parser.h"
#include "concept/query/expr.h"

#include <algorithm>
#include <cctype>
#include <sstream>
#include <unordered_map>
#include <vector>

#include "concept/catalog/catalog_codec.h"
#include "concept/catalog/qualified_name.h"
#include "concept/catalog/value_codec.h"
#include "concept/schema/schema.h"
#include "sql_parse/shared/parse_shared.h"

namespace heterodb::sql_parse {
namespace detail {
std::string AggregateOutputColumn(AggregateFn fn) {
  switch (fn) {
    case AggregateFn::kCount:
      return "count";
    case AggregateFn::kSum:
      return "sum";
    case AggregateFn::kAvg:
      return "avg";
    case AggregateFn::kMin:
      return "min";
    case AggregateFn::kMax:
      return "max";
    case AggregateFn::kGroupConcat:
      return "group_concat";
    default:
      return "agg";
  }
}

std::string WindowOutputColumn(WindowFn fn) {
  switch (fn) {
    case WindowFn::kRowNumber:
      return "row_number";
    case WindowFn::kRank:
      return "rank";
    case WindowFn::kDenseRank:
      return "dense_rank";
    case WindowFn::kLag:
      return "lag";
    case WindowFn::kLead:
      return "lead";
    case WindowFn::kSum:
      return "sum_win";
    case WindowFn::kAvg:
      return "avg_win";
    case WindowFn::kFirstValue:
      return "first_value";
    case WindowFn::kLastValue:
      return "last_value";
    case WindowFn::kNtile:
      return "ntile";
  }
  return "window";
}

Status ParseWindowFrameBound(const std::vector<std::string>& tokens, size_t* pos,
                             WindowFrameBound* bound) {
  if (*pos >= tokens.size()) {
    return Status::Syntax("window frame bound expected", ParseErrorKind::kSyntax);
  }
  if (Upper(tokens[*pos]) == "UNBOUNDED") {
    ++(*pos);
    if (*pos >= tokens.size()) {
      return Status::Syntax("PRECEDING or FOLLOWING expected", ParseErrorKind::kSyntax);
    }
    if (Upper(tokens[*pos]) == "PRECEDING") {
      bound->kind = WindowFrameBoundKind::kUnboundedPreceding;
      ++(*pos);
      return Status::OK();
    }
    if (Upper(tokens[*pos]) == "FOLLOWING") {
      bound->kind = WindowFrameBoundKind::kUnboundedFollowing;
      ++(*pos);
      return Status::OK();
    }
    return Status::Syntax("PRECEDING or FOLLOWING expected after UNBOUNDED", ParseErrorKind::kSyntax);
  }
  if (Upper(tokens[*pos]) == "CURRENT") {
    ++(*pos);
    if (*pos >= tokens.size() || Upper(tokens[*pos]) != "ROW") {
      return Status::Syntax("ROW expected after CURRENT", ParseErrorKind::kSyntax);
    }
    ++(*pos);
    bound->kind = WindowFrameBoundKind::kCurrentRow;
    return Status::OK();
  }
  try {
    bound->offset = static_cast<uint32_t>(std::stoul(tokens[(*pos)++]));
  } catch (...) {
    return Status::Syntax("invalid window frame offset",
                        ParseErrorKind::kInvalidWindowSpec);
  }
  if (*pos >= tokens.size()) {
    return Status::Syntax("PRECEDING or FOLLOWING expected", ParseErrorKind::kSyntax);
  }
  if (Upper(tokens[*pos]) == "PRECEDING") {
    bound->kind = WindowFrameBoundKind::kPreceding;
    ++(*pos);
    return Status::OK();
  }
  if (Upper(tokens[*pos]) == "FOLLOWING") {
    bound->kind = WindowFrameBoundKind::kFollowing;
    ++(*pos);
    return Status::OK();
  }
  return Status::Syntax("PRECEDING or FOLLOWING expected", ParseErrorKind::kSyntax);
}

Status ParseWindowFrame(const std::vector<std::string>& tokens, size_t* pos,
                        WindowFrame* frame) {
  if (*pos >= tokens.size()) {
    return Status::Syntax("window frame expected", ParseErrorKind::kSyntax);
  }
  const std::string units = Upper(tokens[*pos]);
  if (units == "ROWS") {
    frame->units = WindowFrameUnits::kRows;
    ++(*pos);
  } else if (units == "RANGE") {
    frame->units = WindowFrameUnits::kRange;
    ++(*pos);
  } else if (units == "GROUPS") {
    frame->units = WindowFrameUnits::kGroups;
    ++(*pos);
  } else {
    return Status::Syntax("ROWS, RANGE, or GROUPS expected", ParseErrorKind::kSyntax);
  }
  if (*pos >= tokens.size() || Upper(tokens[*pos]) != "BETWEEN") {
    return Status::Syntax("BETWEEN expected", ParseErrorKind::kSyntax);
  }
  ++(*pos);
  Status s = ParseWindowFrameBound(tokens, pos, &frame->start);
  if (!s.ok()) {
    return s;
  }
  if (*pos >= tokens.size() || Upper(tokens[*pos]) != "AND") {
    return Status::Syntax("AND expected in window frame", ParseErrorKind::kSyntax);
  }
  ++(*pos);
  s = ParseWindowFrameBound(tokens, pos, &frame->end);
  if (!s.ok()) {
    return s;
  }
  frame->has_frame = true;
  if (*pos < tokens.size() && Upper(tokens[*pos]) == "EXCLUDE") {
    ++(*pos);
    if (*pos >= tokens.size()) {
      return Status::Syntax("EXCLUDE needs CURRENT ROW or TIES",
                            ParseErrorKind::kSyntax);
    }
    if (Upper(tokens[*pos]) == "TIES") {
      frame->exclusion = WindowFrameExclusion::kExcludeTies;
      ++(*pos);
      return Status::OK();
    }
    if (Upper(tokens[*pos]) == "CURRENT") {
      ++(*pos);
      if (*pos >= tokens.size() || Upper(tokens[*pos]) != "ROW") {
        return Status::Syntax("ROW expected after CURRENT", ParseErrorKind::kSyntax);
      }
      ++(*pos);
      frame->exclusion = WindowFrameExclusion::kExcludeCurrentRow;
      return Status::OK();
    }
    return Status::Syntax("EXCLUDE needs CURRENT ROW or TIES", ParseErrorKind::kSyntax);
  }
  return Status::OK();
}

Status ApplyNamedWindowDef(const std::unordered_map<std::string, WindowExpr>& defs,
                         const std::string& name, WindowExpr* we) {
  const auto it = defs.find(name);
  if (it == defs.end()) {
    return Status::Syntax("unknown window: " + name, ParseErrorKind::kSyntax);
  }
  const WindowFn fn = we->fn;
  const std::string output_column = we->output_column;
  const std::string value_column = we->value_column;
  const uint32_t offset = we->offset;
  const uint32_t ntile_buckets = we->ntile_buckets;
  const std::string default_value = we->default_value;
  *we = it->second;
  we->fn = fn;
  if (!output_column.empty()) {
    we->output_column = output_column;
  }
  if (!value_column.empty()) {
    we->value_column = value_column;
  }
  we->offset = offset;
  we->ntile_buckets = ntile_buckets;
  if (!default_value.empty()) {
    we->default_value = default_value;
  }
  return Status::OK();
}

Status ParseWindowOverBody(const std::vector<std::string>& tokens, size_t* pos,
                           WindowExpr* we) {
  if (*pos < tokens.size() && Upper(tokens[*pos]) == "PARTITION") {
    ++(*pos);
    if (*pos >= tokens.size() || Upper(tokens[*pos]) != "BY") {
      return Status::Syntax("expected BY after PARTITION", ParseErrorKind::kSyntax);
    }
    ++(*pos);
    while (*pos < tokens.size() && Upper(tokens[*pos]) != "ORDER") {
      if (tokens[*pos] == ",") {
        ++(*pos);
        continue;
      }
      we->partition_by.push_back(tokens[(*pos)++]);
    }
  }
  if (*pos >= tokens.size() || Upper(tokens[*pos]) != "ORDER") {
    return Status::Syntax("window OVER requires ORDER BY", ParseErrorKind::kSyntax);
  }
  ++(*pos);
  if (*pos >= tokens.size() || Upper(tokens[*pos]) != "BY") {
    return Status::Syntax("expected BY after ORDER", ParseErrorKind::kSyntax);
  }
  ++(*pos);
  while (*pos < tokens.size() && tokens[*pos] != ")" &&
         Upper(tokens[*pos]) != "ROWS" && Upper(tokens[*pos]) != "RANGE" &&
         Upper(tokens[*pos]) != "GROUPS") {
    if (tokens[*pos] == ",") {
      ++(*pos);
      continue;
    }
    OrderByItem item;
    item.column = tokens[(*pos)++];
    if (*pos < tokens.size() && Upper(tokens[*pos]) == "DESC") {
      item.desc = true;
      ++(*pos);
    } else if (*pos < tokens.size() && Upper(tokens[*pos]) == "ASC") {
      ++(*pos);
    }
    if (*pos < tokens.size() && Upper(tokens[*pos]) == "NULLS") {
      ++(*pos);
      if (*pos >= tokens.size()) {
        return Status::Syntax("NULLS requires FIRST or LAST", ParseErrorKind::kSyntax);
      }
      if (Upper(tokens[*pos]) == "FIRST") {
        item.nulls_first = true;
        ++(*pos);
      } else if (Upper(tokens[*pos]) == "LAST") {
        item.nulls_last = true;
        ++(*pos);
      } else {
        return Status::Syntax("NULLS requires FIRST or LAST", ParseErrorKind::kSyntax);
      }
    }
    we->order_by.push_back(item);
  }
  if (*pos < tokens.size() &&
      (Upper(tokens[*pos]) == "ROWS" || Upper(tokens[*pos]) == "RANGE" ||
       Upper(tokens[*pos]) == "GROUPS")) {
    Status fs = ParseWindowFrame(tokens, pos, &we->frame);
    if (!fs.ok()) {
      return fs;
    }
  }
  if (*pos >= tokens.size() || tokens[*pos] != ")") {
    return Status::Syntax("window OVER missing )", ParseErrorKind::kSyntax);
  }
  ++(*pos);
  return Status::OK();
}


}  // namespace detail
}  // namespace heterodb::sql_parse
