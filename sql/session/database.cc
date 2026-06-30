#include "database.h"

#include <sstream>

#include "attestation.h"

namespace ebtree {
namespace sql {

Database::Database(OpenOptions opts, std::unique_ptr<Engine> engine)
    : options_(std::move(opts)),
      engine_(std::move(engine)),
      catalog_store_(options_.DefaultCatalogPath()),
      op_log_(std::make_unique<OpLogWriter>(options_.DefaultOpLogPath())),
      executor_(engine_.get(), &catalog_, &catalog_store_, op_log_.get(),
                options_.durability, &txn_) {
  (void)catalog_store_.Load(&catalog_);
  InstallGroupCommitObserver();
}

void Database::InstallGroupCommitObserver() {
  if (options_.durability != DurabilityClass::kGroup || !op_log_) return;
  engine_->SetGroupCommitObserver([this](Engine* eng) {
    (void)eng;
    if (op_log_) {
      (void)op_log_->MarkDurableThroughLsn(engine_->stable_lsn());
    }
  });
}

Status Database::Open(const OpenOptions& opts,
                      std::unique_ptr<Database>* out) {
  if (!out) return Status::InvalidArgument("out is null");

  if (opts.attestation != AttestationMode::kOff) {
    AttestationReport report{};
    std::unique_ptr<Engine> engine;
    const Status st = RunOpenAttestation(opts, &report, &engine);
    if (!st.ok()) return st;
    if (!AttestationAllowsOpen(opts.attestation, report.verdict)) {
      return Status::Corrupt("attestation refused: " + report.verdict_reason);
    }
    if (report.any_badwal &&
        opts.attestation == AttestationMode::kRequirePass) {
      return Status::Corrupt("attestation refused: badwal_marker");
    }
    if (!engine) {
      return Status::Internal("attestation passed but engine missing");
    }
    out->reset(new Database(opts, std::move(engine)));
    return Status::Ok();
  }

  std::unique_ptr<Engine> engine;
  const Status st = Engine::Open(opts.ToEngineOptions(), &engine);
  if (!st.ok()) return st;
  out->reset(new Database(opts, std::move(engine)));
  return Status::Ok();
}

Status Database::ExecuteSql(const std::string& sql, ExecuteResult* result) {
  QueryStatement stmt{};
  const Status ps = parser_.Parse(sql, &stmt);
  if (!ps.ok()) {
    last_error_ = ps.message();
    return ps;
  }
  const Status es = Execute(stmt, result);
  if (!es.ok()) last_error_ = es.message();
  return es;
}

static std::string SavepointNameFromSql(const std::string& raw_sql) {
  std::istringstream iss(raw_sql);
  std::string token;
  iss >> token;
  if (!iss) return "sp";
  std::string name;
  iss >> name;
  return name.empty() ? "sp" : name;
}

Status Database::Execute(const QueryStatement& stmt, ExecuteResult* result) {
  if (stmt.kind == QueryStmtKind::kOpen) {
    return Status::InvalidArgument("OPEN must use Database::Open");
  }
  if (stmt.kind == QueryStmtKind::kPrepare) {
    if (stmt.prepare.name.empty() || stmt.prepare.sql.empty()) {
      return Status::InvalidArgument("invalid PREPARE");
    }
    prepared_[stmt.prepare.name] = stmt.prepare.sql;
    return Status::Ok();
  }
  if (stmt.kind == QueryStmtKind::kExecute) {
    const auto it = prepared_.find(stmt.execute.name);
    if (it == prepared_.end()) {
      return Status::InvalidArgument("unknown prepared statement");
    }
    return ExecuteSql(it->second, result);
  }
  if (stmt.kind == QueryStmtKind::kBeginTxn) {
    return txn_.Begin();
  }
  if (stmt.kind == QueryStmtKind::kCommit) {
    return txn_.Commit(engine_.get(), options_.durability);
  }
  if (stmt.kind == QueryStmtKind::kRollback) {
    return txn_.Rollback(engine_.get());
  }
  if (stmt.kind == QueryStmtKind::kSavepoint) {
    return txn_.Savepoint(SavepointNameFromSql(stmt.raw_sql));
  }
  const Status st = executor_.Execute(stmt, result);
  if (!st.ok()) last_error_ = st.message();
  if (st.ok()) {
    std::string trig_table;
    std::string trig_event;
    if (stmt.kind == QueryStmtKind::kInsert) {
      trig_table = stmt.insert.table;
      trig_event = "INSERT";
    } else if (stmt.kind == QueryStmtKind::kUpsert) {
      trig_table = stmt.upsert.table.empty() ? stmt.insert.table : stmt.upsert.table;
      trig_event = "INSERT";
    } else if (stmt.kind == QueryStmtKind::kUpdate) {
      trig_table = stmt.update.table;
      trig_event = "UPDATE";
    } else if (stmt.kind == QueryStmtKind::kDelete) {
      trig_table = stmt.delete_stmt.table;
      trig_event = "DELETE";
    }
    if (!trig_table.empty()) {
      for (const auto& tr : catalog_.TriggersForTable(trig_table, trig_event)) {
        (void)ExecuteSql(tr.body_sql, nullptr);
      }
    }
  }
  return st;
}

void Database::Close() {
  if (op_log_) op_log_->Flush();
  (void)catalog_store_.Save(catalog_);
  engine_.reset();
}

}  // namespace sql
}  // namespace ebtree
