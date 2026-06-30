#pragma once

#include "sql_parse/core/parse_registry.h"

namespace heterodb::sql_parse {

void RegisterStmtRuleSelect(FirstMatchRegistry* routes);
void RegisterStmtRuleDml(FirstMatchRegistry* routes);
void RegisterStmtRuleDdl(FirstMatchRegistry* routes);
void RegisterStmtRuleTxn(FirstMatchRegistry* routes);
void RegisterStmtRuleMeta(FirstMatchRegistry* routes);
void RegisterStmtRulePriv(FirstMatchRegistry* routes);
void RegisterStmtRuleSet(FirstMatchRegistry* routes);

}  // namespace heterodb::sql_parse
