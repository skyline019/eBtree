#pragma once

#include "sql_parse/core/parse_registry.h"
#include "sql_parse/expr/expr_registry.h"
#include "sql_parse/pred/pred_registry.h"

namespace heterodb::sql_parse {

class StmtRegistry {
 public:
  FirstMatchRegistry& routes() { return routes_; }
  const FirstMatchRegistry& routes() const { return routes_; }
  ExprRegistry& expr() { return expr_; }
  PredRegistry& pred() { return pred_; }

 private:
  FirstMatchRegistry routes_;
  ExprRegistry expr_;
  PredRegistry pred_;
};

}  // namespace heterodb::sql_parse
