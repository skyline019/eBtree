#include "sql_parse/shared/parse_shared.h"

#include "sql_parse/shared/ident_normalize.h"

#include <cctype>

namespace heterodb::sql_parse {

std::string Trim(std::string s) {
  while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front()))) {
    s.erase(s.begin());
  }
  while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back()))) {
    s.pop_back();
  }
  return s;
}

std::string Upper(std::string s) {
  for (char& c : s) {
    c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
  }
  return s;
}

bool SqlInputNeedsNormalization(const std::string& sql) {
  bool in_quote = false;
  for (size_t i = 0; i < sql.size(); ++i) {
    const char c = sql[i];
    if (c == '\'') {
      if (in_quote && i + 1 < sql.size() && sql[i + 1] == '\'') {
        ++i;
        continue;
      }
      in_quote = !in_quote;
      continue;
    }
    if (!in_quote) {
      if (c == '-' && i + 1 < sql.size() && sql[i + 1] == '-') {
        return true;
      }
      if (c == '/' && i + 1 < sql.size() && sql[i + 1] == '*') {
        return true;
      }
      if (c == '#') {
        return true;
      }
    }
    if (!in_quote && std::isspace(static_cast<unsigned char>(c))) {
      return true;
    }
  }
  return !sql.empty() && sql.back() == ';';
}

std::string NormalizeSqlInput(std::string sql) {
  std::string out;
  out.reserve(sql.size());
  bool in_single = false;
  bool in_double = false;
  for (size_t i = 0; i < sql.size(); ++i) {
    const char c = sql[i];
    if (c == '\'') {
      if (in_single && i + 1 < sql.size() && sql[i + 1] == '\'') {
        out += "''";
        ++i;
        continue;
      }
      in_single = !in_single;
      out += c;
      continue;
    }
    if (c == '"') {
      if (in_double && i + 1 < sql.size() && sql[i + 1] == '"') {
        out += "\"\"";
        ++i;
        continue;
      }
      in_double = !in_double;
      out += c;
      continue;
    }
    if (!in_single && !in_double && c == '#') {
      while (i < sql.size() && sql[i] != '\n' && sql[i] != '\r') {
        ++i;
      }
      if (i < sql.size()) {
        out += '\n';
      }
      continue;
    }
    if (!in_single && !in_double && c == '-' && i + 1 < sql.size() && sql[i + 1] == '-') {
      while (i < sql.size() && sql[i] != '\n' && sql[i] != '\r') {
        ++i;
      }
      if (i < sql.size()) {
        out += '\n';
      }
      continue;
    }
    if (!in_single && !in_double && c == '/' && i + 1 < sql.size() && sql[i + 1] == '*') {
      i += 2;
      while (i + 1 < sql.size() && !(sql[i] == '*' && sql[i + 1] == '/')) {
        ++i;
      }
      if (i + 1 < sql.size()) {
        i += 1;
      }
      continue;
    }
    out += c;
  }
  out = Trim(out);
  while (!out.empty() && out.back() == ';') {
    out.pop_back();
    out = Trim(out);
  }
  return out;
}

namespace {

bool IsDelimiter(char c) { return c == '(' || c == ')' || c == ','; }

bool IsIdentifierChar(char c) {
  return std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '.' ||
         c == '@';
}

std::vector<std::string> ExpandGluedOperatorTokens(
    std::vector<std::string> tokens) {
  std::vector<std::string> out;
  out.reserve(tokens.size() + 4);
  for (const std::string& tok : tokens) {
    if (tok.empty() || tok == "(" || tok == ")" || tok == ",") {
      out.push_back(tok);
      continue;
    }
    if (tok.front() == '\'' || tok.front() == '"') {
      out.push_back(tok);
      continue;
    }
    bool needs_split = false;
    for (size_t i = 0; i < tok.size(); ++i) {
      const char c = tok[i];
      if (c == '*' && i > 0 && tok[i - 1] == '.') {
        continue;
      }
      if (!IsIdentifierChar(c) && c != '(' && c != ')') {
        needs_split = true;
        break;
      }
    }
    if (!needs_split) {
      out.push_back(tok);
      continue;
    }
    std::string part;
    auto flush = [&]() {
      if (!part.empty()) {
        out.push_back(part);
        part.clear();
      }
    };
    for (size_t i = 0; i < tok.size(); ++i) {
      const char c = tok[i];
      if (c == '<' && i + 1 < tok.size() && tok[i + 1] == '=') {
        flush();
        out.push_back("<=");
        ++i;
        continue;
      }
      if (c == '>' && i + 1 < tok.size() && tok[i + 1] == '=') {
        flush();
        out.push_back(">=");
        ++i;
        continue;
      }
      if (c == '!' && i + 1 < tok.size() && tok[i + 1] == '=') {
        flush();
        out.push_back("!=");
        ++i;
        continue;
      }
      if (c == '<' && i + 1 < tok.size() && tok[i + 1] == '>') {
        flush();
        out.push_back("<>");
        ++i;
        continue;
      }
      if (c == '-' && i + 1 < tok.size() && tok[i + 1] == '>') {
        flush();
        if (i + 2 < tok.size() && tok[i + 2] == '>') {
          out.push_back("->>");
          i += 2;
        } else {
          out.push_back("->");
          ++i;
        }
        continue;
      }
      if ((c == '+' || c == '-') && part.empty() && i + 1 < tok.size() &&
          std::isdigit(static_cast<unsigned char>(tok[i + 1]))) {
        part.push_back(c);
        continue;
      }
      if (c == '+' || c == '-' || c == '*' || c == '/' || c == '=' ||
          c == '<' || c == '>' || c == '!' || c == '(' || c == ')') {
        flush();
        out.push_back(std::string(1, c));
        continue;
      }
      part.push_back(c);
    }
    flush();
  }
  return out;
}

}  // namespace

std::vector<std::string> SplitTokens(const std::string& sql) {
  std::vector<std::string> tokens;
  std::string cur;
  bool in_single = false;
  bool in_double = false;
  bool in_backtick = false;
  for (size_t i = 0; i < sql.size(); ++i) {
    const char c = sql[i];
    if (c == '\'') {
      in_single = !in_single;
      cur.push_back(c);
      continue;
    }
    if (c == '"' && !in_single) {
      in_double = !in_double;
      cur.push_back(c);
      continue;
    }
    if (c == '`' && !in_single && !in_double) {
      in_backtick = !in_backtick;
      cur.push_back(c);
      continue;
    }
    if (!in_single && !in_double && !in_backtick &&
        (std::isspace(static_cast<unsigned char>(c)) || IsDelimiter(c))) {
      if (!cur.empty()) {
        tokens.push_back(StripBackticks(cur));
        cur.clear();
      }
      if (!in_single && !in_double && !in_backtick && IsDelimiter(c)) {
        tokens.push_back(std::string(1, c));
      }
      continue;
    }
    cur.push_back(c);
  }
  if (!cur.empty()) {
    tokens.push_back(StripBackticks(cur));
  }
  return ExpandGluedOperatorTokens(std::move(tokens));
}

std::vector<std::string> SplitSqlStatements(const std::string& sql) {
  std::vector<std::string> parts;
  std::string cur;
  bool in_single = false;
  bool in_double = false;
  for (size_t i = 0; i < sql.size(); ++i) {
    const char c = sql[i];
    if (c == '\'') {
      in_single = !in_single;
      cur.push_back(c);
      continue;
    }
    if (c == '"' && !in_single) {
      in_double = !in_double;
      cur.push_back(c);
      continue;
    }
    if (!in_single && !in_double && c == ';') {
      std::string piece = Trim(cur);
      if (!piece.empty()) {
        parts.push_back(std::move(piece));
      }
      cur.clear();
      continue;
    }
    cur.push_back(c);
  }
  std::string piece = Trim(cur);
  if (!piece.empty()) {
    parts.push_back(std::move(piece));
  }
  return parts;
}

}  // namespace heterodb::sql_parse
