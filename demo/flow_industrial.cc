#include "demo_flows.h"

#include "rar_chain.h"
#include "rar_chain_anchor.h"
#include "rar_monitor.h"

#include "ebtree/engine/engine.h"
#include "sql/session/database.h"

#include <chrono>
#include <cstdio>
#include <filesystem>
#include <thread>

namespace ebtree {
namespace demo {

namespace {

void StepOk(const char* name) { std::printf("STEP_OK %s\n", name); }
void StepFail(const char* name, const char* detail) {
  std::printf("STEP_FAIL %s: %s\n", name, detail ? detail : "");
}

bool WaitForChainEntry(const std::string& chain_path,
                       std::chrono::milliseconds timeout) {
  const auto deadline = std::chrono::steady_clock::now() + timeout;
  while (std::chrono::steady_clock::now() < deadline) {
    audit::RarChainEntry entry{};
    bool found = false;
    if (audit::ReadLastRarChainEntry(chain_path, &entry, &found).ok() &&
        found) {
      return true;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
  }
  return false;
}

}  // namespace

int RunIndustrialFlow(const std::string& dir) {
  std::error_code ec;
  std::filesystem::remove_all(dir, ec);
  std::filesystem::create_directories(dir, ec);

  const std::string chain_path = dir + "/ebtree.rar.chain.jsonl";
  const std::string anchor_dir = dir + "/carl_anchors";

  EngineOptions opts = EngineOptions::ProductionDefaults(dir);
  audit::RarMonitorOptions rar_opts{};
  rar_opts.enabled = true;
  rar_opts.chain_path = chain_path;
  rar_opts.write_circuit = true;
  rar_opts.runtime_policy.require_unexpected_path_zero = true;

  std::unique_ptr<Engine> engine;
  audit::RarMonitor monitor;
  if (!audit::OpenWithRarMonitor(opts, rar_opts, &engine, &monitor).ok()) {
    StepFail("open_monitor", "OpenWithRarMonitor");
    return 1;
  }
  StepOk("open_monitor");

  for (int i = 0; i < 1000; ++i) {
    if (!engine->Put("cfg" + std::to_string(i), "v" + std::to_string(i)).ok()) {
      StepFail("kv_put", "Put failed");
      return 1;
    }
  }
  if (!engine->Checkpoint().ok()) {
    StepFail("checkpoint", "Checkpoint failed");
    return 1;
  }
  StepOk("kv_put_checkpoint");

  if (!WaitForChainEntry(chain_path, std::chrono::seconds(5))) {
    StepFail("chain_wait", "no chain entry");
    return 1;
  }
  StepOk("chain_append");

  audit::CarlSignedTreeHead sth{};
  if (!audit::PublishCarlAnchor(chain_path, anchor_dir, &sth).ok()) {
    StepFail("anchor_publish", "PublishCarlAnchor");
    return 1;
  }
  StepOk("anchor_publish");

  monitor.Stop();
  engine.reset();

  if (!audit::OpenWithRarMonitor(opts, rar_opts, &engine, &monitor).ok()) {
    StepFail("fast_open", "reopen failed");
    return 1;
  }
  StepOk("fast_open");

  audit::RarChainVerifyReport report{};
  if (!audit::VerifyRarChain(chain_path, &report).ok() || !report.consistent) {
    StepFail("chain_verify", "inconsistent");
    return 1;
  }
  StepOk("chain_verify");

  if (!audit::VerifyCarlAnchorRequired(chain_path, anchor_dir).ok()) {
    StepFail("anchor_verify", "anchor mismatch");
    return 1;
  }
  StepOk("anchor_verify");

  if (EngineStats* stats = engine->mutable_stats()) {
    ++stats->unexpected_path_total;
  }
  monitor.RefreshRuntimeState();
  if (monitor.AllowsWrite()) {
    StepFail("write_circuit", "still allows write");
    return 1;
  }
  StepOk("write_circuit");
  std::string value;
  if (!engine->Get("cfg0", &value).ok()) {
    StepFail("read_after_circuit", "Get failed");
    return 1;
  }
  StepOk("write_circuit_read_ok");

  monitor.Stop();
  engine.reset();

  {
    sql::OpenOptions sql_opts{};
    sql_opts.path = dir + "/sql_circuit";
    sql_opts.attestation = sql::AttestationMode::kMonitor;
    std::unique_ptr<sql::Database> db;
    if (!sql::Database::Open(sql_opts, &db).ok()) {
      StepFail("sql_open", "Database::Open");
      return 1;
    }
    StepOk("sql_open");
    if (!db->ExecuteSql(
              "CREATE TABLE config (key TEXT PRIMARY KEY, value TEXT)")
             .ok()) {
      StepFail("sql_create", db->last_error().c_str());
      return 1;
    }
    if (db->engine()->mutable_stats()) {
      ++db->engine()->mutable_stats()->unexpected_path_total;
    }
    const Status insert_st = db->ExecuteSql(
        "INSERT INTO config (key, value) VALUES ('forbidden', 'x')");
    if (insert_st.ok()) {
      StepFail("sql_write_circuit", "INSERT should fail");
      return 1;
    }
    StepOk("sql_write_circuit");
    sql::ExecuteResult select{};
    if (!db->ExecuteSql("SELECT key FROM config", &select).ok()) {
      StepFail("sql_read_after_circuit", db->last_error().c_str());
      return 1;
    }
    StepOk("sql_read_after_circuit");
  }

  StepOk("industrial_complete");
  return 0;
}

}  // namespace demo
}  // namespace ebtree
