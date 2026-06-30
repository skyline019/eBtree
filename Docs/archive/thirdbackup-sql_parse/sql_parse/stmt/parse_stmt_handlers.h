#pragma once

#include "common/status.h"
#include "sql_parse/core/parse_context.h"

namespace heterodb::sql_parse {

Status DispatchSelectStatement(ParseContext* ctx);
Status DispatchDmlStatement(ParseContext* ctx);
Status DispatchDdlStatement(ParseContext* ctx);
Status DispatchTxnStatement(ParseContext* ctx);
Status DispatchMetaStatement(ParseContext* ctx);
Status DispatchPrivStatement(ParseContext* ctx);
Status DispatchSetStatement(ParseContext* ctx);

}  // namespace heterodb::sql_parse
