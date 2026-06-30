#pragma once

namespace heterodb::sql_parse {

class ExprRegistry;
class FirstMatchRegistry;
class LexPipeline;
class PredRegistry;
class StmtRegistry;

void RegisterLexRules(LexPipeline* pipeline);
void RegisterExprPlugins(ExprRegistry* registry);
void RegisterPredRules(PredRegistry* registry);
void RegisterStmtRoutes(StmtRegistry* registry);
void InstallRouterStmtFallback(FirstMatchRegistry* routes);

}  // namespace heterodb::sql_parse
