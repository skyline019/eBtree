#include "demo_flows.h"

#include "rar_chain.h"

#include "sql/session/database.h"

#include <chrono>
#include <cstdio>
#include <filesystem>

namespace ebtree {
namespace demo {

namespace {

void StepOk(const char* name) { std::printf("STEP_OK %s\n", name); }
void StepFail(const char* name, const char* detail) {
  std::printf("STEP_FAIL %s: %s\n", name, detail ? detail : "");
}

int64_t OpenMonitorMs(const std::string& dir) {
  sql::OpenOptions opts{};
  opts.path = dir;
  opts.durability = DurabilityClass::kSync;
  opts.attestation = sql::AttestationMode::kMonitor;
  const auto start = std::chrono::steady_clock::now();
  std::unique_ptr<sql::Database> db;
  if (!sql::Database::Open(opts, &db).ok()) return -1;
  return std::chrono::duration_cast<std::chrono::milliseconds>(
             std::chrono::steady_clock::now() - start)
      .count();
}

int64_t OpenRequirePassMs(const std::string& dir) {
  sql::OpenOptions opts{};
  opts.path = dir;
  opts.durability = DurabilityClass::kSync;
  opts.attestation = sql::AttestationMode::kRequirePass;
  const auto start = std::chrono::steady_clock::now();
  std::unique_ptr<sql::Database> db;
  if (!sql::Database::Open(opts, &db).ok()) return -1;
  return std::chrono::duration_cast<std::chrono::milliseconds>(
             std::chrono::steady_clock::now() - start)
      .count();
}

}  // namespace

int RunFinanceFlow(const std::string& dir) {
  std::error_code ec;
  std::filesystem::remove_all(dir, ec);
  std::filesystem::create_directories(dir, ec);

  {
    sql::OpenOptions seed{};
    seed.path = dir;
    seed.durability = DurabilityClass::kSync;
    seed.attestation = sql::AttestationMode::kOff;
    std::unique_ptr<sql::Database> db;
    if (!sql::Database::Open(seed, &db).ok()) {
      StepFail("seed_open", "seed Database::Open");
      return 1;
    }
    if (!db->ExecuteSql(
              "CREATE TABLE ledger (key TEXT PRIMARY KEY, value TEXT)")
             .ok()) {
      StepFail("seed_create", db->last_error().c_str());
      return 1;
    }
    if (!db->ExecuteSql("INSERT INTO ledger (key, value) VALUES ('edge1', '100')")
             .ok()) {
      StepFail("seed_insert", db->last_error().c_str());
      return 1;
    }
    if (!db->engine()->Checkpoint().ok()) {
      StepFail("seed_checkpoint", "Checkpoint");
      return 1;
    }
  }
  StepOk("seed_ksync_data");

  const int64_t monitor_ms = OpenMonitorMs(dir);
  if (monitor_ms < 0) {
    StepFail("monitor_open", "Open failed");
    return 1;
  }
  std::printf("METRIC monitor_open_ms=%lld\n", static_cast<long long>(monitor_ms));
  StepOk("monitor_open");

  const int64_t require_ms = OpenRequirePassMs(dir);
  if (require_ms < 0) {
    StepFail("require_pass_open", "Open failed");
    return 1;
  }
  std::printf("METRIC require_pass_open_ms=%lld\n",
              static_cast<long long>(require_ms));
  StepOk("require_pass_open");

  const std::string chain_path = dir + "/ebtree.rar.chain.jsonl";
  audit::RarChainEntry tail{};
  bool has_chain = false;
  if (audit::ReadLastRarChainEntry(chain_path, &tail, &has_chain).ok() &&
      has_chain) {
    audit::RarChainVerifyReport report{};
    if (audit::VerifyRarChain(chain_path, &report).ok() && report.consistent) {
      StepOk("chain_consistent");
    } else {
      StepFail("chain_verify", "inconsistent");
      return 1;
    }
  } else {
    StepOk("chain_absent_or_empty");
  }

  std::printf("METRIC open_latency_ratio=%.3f\n",
              monitor_ms > 0 ? static_cast<double>(require_ms) /
                                   static_cast<double>(monitor_ms)
                             : 0.0);
  StepOk("finance_complete");
  return 0;
}

}  // namespace demo
}  // namespace ebtree
