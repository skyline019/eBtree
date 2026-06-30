#include "pragma_exec.h"

#include "sql/exec/executor.h"
#include "sql/parse/shared/parse_shared.h"

namespace ebtree {
namespace sql {

Status ExecPragma(const QueryStatement& stmt, Catalog* catalog,
                  ExecuteResult* out) {
  if (!catalog) return Status::InvalidArgument("null catalog");
  const std::string u = parse::Upper(parse::Trim(stmt.raw_sql));
  if (u.find("PRAGMA TABLE_INFO(") == 0 ||
      u.find("PRAGMA TABLE_INFO (") == 0) {
    const auto lp = stmt.raw_sql.find('(');
    const auto rp = stmt.raw_sql.rfind(')');
    if (lp == std::string::npos || rp == std::string::npos) {
      return Status::InvalidArgument("PRAGMA table_info syntax");
    }
    std::string table = parse::Trim(stmt.raw_sql.substr(lp + 1, rp - lp - 1));
    if (!table.empty() && table.front() == '\'') table = parse::UnquoteToken(table);
    const auto schema = catalog->FindTable(table);
    if (!schema) return Status::InvalidArgument("unknown table: " + table);
    if (out) {
      int cid = 0;
      for (const auto& col : schema->columns) {
        SqlRow row{};
        row.key = std::to_string(cid);
        row.value = col.name + "|" + col.type + "|" +
                    (col.not_null ? "1" : "0") + "||" +
                    (col.primary_key ? "1" : "0");
        out->rows.push_back(row);
        ++cid;
      }
    }
    return Status::Ok();
  }
  return Status::InvalidArgument("unsupported PRAGMA");
}

}  // namespace sql
}  // namespace ebtree
