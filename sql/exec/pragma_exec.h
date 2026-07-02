#pragma once

#include <string>
#include <vector>

#include "sql/catalog/catalog.h"
#include "sql/ast/query_ast.h"
#include "sql/exec/executor.h"
#include "ebtree/common/status.h"

namespace ebtree {
namespace audit {
class RarMonitor;
}  // namespace audit

namespace sql {

Status ExecPragma(const QueryStatement& stmt, Catalog* catalog, ExecuteResult* out);
Status ExecRarStatusPragma(const audit::RarMonitor& monitor, ExecuteResult* out);

}  // namespace sql
}  // namespace ebtree
