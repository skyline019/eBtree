#include "sql_parse/expr/expr_engine.h"

#include "common/parse_error.h"

#include "sql_parse/expr/expr_parse.h"

namespace heterodb::sql_parse {

Status ExprEngine::ParseExpr(TokenCursor* cursor, Expr** out,
                             const std::vector<std::string>& stop_tokens) {
  if (cursor == nullptr || out == nullptr) {
    return Status::InvalidArgument("null cursor or output");
  }
  size_t pos = cursor->pos();
  std::vector<std::string> tokens = cursor->tokens();
  for (const std::string& st : stop_tokens) {
    if (pos < tokens.size() && tokens[pos] == st) {
      return Status::Syntax("incomplete expression", ParseErrorKind::kSyntax);
    }
    if (pos < tokens.size() && IsExprStopToken(tokens[pos])) {
      return Status::Syntax("incomplete expression", ParseErrorKind::kSyntax);
    }
  }
  Status s = heterodb::sql_parse::ParseExpr(tokens, &pos, out);
  if (!s.ok()) {
    return s;
  }
  cursor->set_pos(pos);
  return Status::OK();
}

}  // namespace heterodb::sql_parse
