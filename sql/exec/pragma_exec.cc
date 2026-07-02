#include "pragma_exec.h"

#include "sql/exec/executor.h"
#include "sql/parse/shared/parse_shared.h"
#include "rar_monitor.h"

#include "sql/parse/shared/parse_shared.h"

namespace ebtree {
namespace sql {

namespace {

bool IsRarStatusPragma(const std::string& raw_sql) {
  std::string u = parse::Upper(parse::Trim(raw_sql));
  while (!u.empty() && (u.back() == ';' || u.back() == ' ')) {
    u.pop_back();
  }
  return u == "PRAGMA RAR_STATUS";
}

void FillRarStatusRows(const audit::RarStatusSnapshot& snap, ExecuteResult* out) {
  if (!out) return;
  auto row = [&](const std::string& key, const std::string& value) {
    SqlRow r{};
    r.key = key;
    r.value = value;
    out->rows.push_back(std::move(r));
  };
  row("allows_write", snap.allows_write ? "1" : "0");
  row("unexpected_path_total", std::to_string(snap.unexpected_path_total));
  row("decompress_fail_total", std::to_string(snap.decompress_fail_total));
  row("rar_chain_drop_total", std::to_string(snap.rar_chain_drop_total));
  row("last_chain_sequence", std::to_string(snap.last_chain_sequence));
  row("last_anchor_sequence", std::to_string(snap.last_anchor_sequence));
  row("last_anchor_hash", snap.last_anchor_hash);
  row("last_chain_verdict", snap.last_chain_verdict);
  row("last_chain_reason", snap.last_chain_reason);
  row("startup_chain_consistent", snap.startup_chain_consistent ? "1" : "0");
  row("worker_running", snap.worker_running ? "1" : "0");
}

}  // namespace

Status ExecRarStatusPragma(const audit::RarMonitor& monitor, ExecuteResult* out) {
  FillRarStatusRows(monitor.StatusSnapshot(), out);
  return Status::Ok();
}

Status ExecPragma(const QueryStatement& stmt, Catalog* catalog,
                  ExecuteResult* out) {
  if (!catalog) return Status::InvalidArgument("null catalog");
  const std::string u = parse::Upper(parse::Trim(stmt.raw_sql));
  if (IsRarStatusPragma(stmt.raw_sql)) {
    return Status::InvalidArgument(
        "PRAGMA rar_status requires Database session");
  }
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
