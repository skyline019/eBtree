#pragma once

#include "sql_parse/core/parse_registry.h"

namespace heterodb::sql_parse {

/** Expr plugins (Pratt extensions); core engine in expr_engine (future). */
class ExprRegistry {
 public:
  FirstMatchRegistry& plugins() { return plugins_; }
  const FirstMatchRegistry& plugins() const { return plugins_; }

 private:
  FirstMatchRegistry plugins_;
};

}  // namespace heterodb::sql_parse
