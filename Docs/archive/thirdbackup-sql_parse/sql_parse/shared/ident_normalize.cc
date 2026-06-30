#include "sql_parse/shared/ident_normalize.h"

namespace heterodb::sql_parse {

std::string StripBackticks(std::string tok) {
  if (tok.size() >= 2 && tok.front() == '`' && tok.back() == '`') {
    return tok.substr(1, tok.size() - 2);
  }
  return tok;
}

std::string NormalizeIdent(const std::string& tok) {
  return StripBackticks(tok);
}

}  // namespace heterodb::sql_parse
