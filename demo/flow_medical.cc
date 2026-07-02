#include "demo_flows.h"

#include "rar_chain_anchor.h"
#include "rar_monitor.h"

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

}  // namespace

int RunMedicalFlow(const std::string& dir) {
  std::error_code ec;
  std::filesystem::remove_all(dir, ec);
  std::filesystem::create_directories(dir, ec);

  sql::OpenOptions opts{};
  opts.path = dir;
  opts.attestation = sql::AttestationMode::kMonitor;

  std::unique_ptr<sql::Database> db;
  if (!sql::Database::Open(opts, &db).ok()) {
    StepFail("sql_open", "Database::Open");
    return 1;
  }
  StepOk("sql_open");

  if (!db->ExecuteSql(
            "CREATE TABLE records (key TEXT PRIMARY KEY, value TEXT)")
           .ok()) {
    StepFail("create_table", db->last_error().c_str());
    return 1;
  }
  for (int i = 0; i < 50; ++i) {
    const std::string sql =
        "INSERT INTO records (key, value) VALUES ('k" + std::to_string(i) +
        "', 'v" + std::to_string(i) + "')";
    if (!db->ExecuteSql(sql).ok()) {
      StepFail("insert", db->last_error().c_str());
      return 1;
    }
  }
  StepOk("sql_insert");

  if (!db->engine()->Checkpoint().ok()) {
    StepFail("checkpoint", "Checkpoint");
    return 1;
  }

  const std::string chain_path = opts.DefaultRarChainPath();
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
  bool chain_ready = false;
  while (std::chrono::steady_clock::now() < deadline) {
    const audit::RarStatusSnapshot snap = db->rar_monitor().StatusSnapshot();
    if (snap.last_chain_sequence > 0) {
      chain_ready = true;
      break;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
  }
  if (!chain_ready) {
    StepFail("chain_wait", "last_chain_sequence=0");
    return 1;
  }
  StepOk("chain_ready");

  sql::ExecuteResult pragma{};
  if (!db->ExecuteSql("PRAGMA rar_status", &pragma).ok()) {
    StepFail("pragma_rar_status", db->last_error().c_str());
    return 1;
  }
  bool allows = false;
  for (const auto& row : pragma.rows) {
    if (row.key == "allows_write" && row.value == "1") allows = true;
  }
  if (!allows) {
    StepFail("allows_write", "expected 1");
    return 1;
  }
  StepOk("pragma_rar_status");

  const std::string anchor_dir = dir + "/carl_anchors";
  audit::CarlSignedTreeHead sth{};
  if (!audit::PublishCarlAnchor(chain_path, anchor_dir, &sth).ok()) {
    StepFail("anchor_publish", "PublishCarlAnchor");
    return 1;
  }
  StepOk("anchor_publish");

  if (!audit::VerifyCarlAnchorRequired(chain_path, anchor_dir).ok()) {
    StepFail("anchor_verify", "VerifyCarlAnchorRequired");
    return 1;
  }
  StepOk("anchor_verify");

  const audit::RarStatusSnapshot after = db->rar_monitor().StatusSnapshot();
  if (after.last_anchor_sequence == 0 || after.last_anchor_hash.empty()) {
    StepFail("anchor_status", "missing anchor fields in snapshot");
    return 1;
  }
  StepOk("medical_complete");
  return 0;
}

}  // namespace demo
}  // namespace ebtree
