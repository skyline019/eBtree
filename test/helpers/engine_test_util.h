#pragma once

#include <atomic>
#include <cstdlib>
#include <filesystem>
#include <memory>
#include <string>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <process.h>
#else
#include <unistd.h>
#endif

#include "ebtree/engine/engine.h"

namespace ebtree {
namespace test {

inline std::filesystem::path TestTmpRoot() {
  if (const char* env = std::getenv("EBTREE_TEST_TMP_ROOT")) {
    if (env[0] != '\0') {
      return std::filesystem::path(env);
    }
  }
  return std::filesystem::current_path() / ".test-runs" / "tmp" / "default";
}

inline std::string TempDir(const std::string& name) {
  static std::atomic<uint64_t> seq{0};
  const auto root = TestTmpRoot();
  std::filesystem::create_directories(root);
#if defined(_WIN32)
  const uint64_t owner = static_cast<uint64_t>(_getpid());
#else
  const uint64_t owner = static_cast<uint64_t>(getpid());
#endif
  const auto base = root / (name + "_" + std::to_string(owner) + "_" +
                            std::to_string(++seq));
  std::error_code ec;
  std::filesystem::remove_all(base, ec);
  std::filesystem::create_directories(base);
  return base.string();
}

inline std::unique_ptr<Engine> OpenEngineWithOptions(
    const std::string& dir, const EngineOptions& opts) {
  EngineOptions open_opts = opts;
  open_opts.path = dir;
  std::unique_ptr<Engine> engine;
  if (!Engine::Open(open_opts, &engine).ok()) {
    return nullptr;
  }
  return engine;
}

inline EngineOptions TestBalancedOptions(const std::string& dir, bool raw_bench) {
  EngineOptions opts =
      raw_bench ? EngineOptions::ProductionDefaults(dir)
                : EngineOptions::StandardDefaults(dir);
  opts.background_summary_validate = false;
  opts.background_flush = false;
  return opts;
}

inline std::unique_ptr<Engine> OpenEngine(
    const std::string& dir,
    DurabilityClass durability = DurabilityClass::kBalanced,
    bool compress_pages = false) {
  if (durability == DurabilityClass::kBalanced) {
    EngineOptions opts = TestBalancedOptions(dir, false);
    opts.compress_pages = compress_pages;
    return OpenEngineWithOptions(dir, opts);
  }
  if (durability == DurabilityClass::kSync) {
    EngineOptions opts = EngineOptions::EnterpriseDefaults(dir);
    opts.background_summary_validate = false;
    opts.background_flush = false;
    opts.compress_pages = compress_pages;
    return OpenEngineWithOptions(dir, opts);
  }
  EngineOptions opts = EngineOptions::BenchmarkGroupDefaults(dir);
  opts.path = dir;
  opts.durability = durability;
  opts.recovery_strategy = RecoveryStrategy::kFastOpen;
  opts.background_summary_validate = false;
  opts.background_flush = false;
  opts.compress_pages = compress_pages;
  return OpenEngineWithOptions(dir, opts);
}

inline std::unique_ptr<Engine> OpenBenchEngine(
    const std::string& dir,
    DurabilityClass durability = DurabilityClass::kBalanced,
    bool compress_pages = false) {
  if (durability == DurabilityClass::kBalanced) {
    EngineOptions opts = TestBalancedOptions(dir, true);
    opts.compress_pages = compress_pages;
    return OpenEngineWithOptions(dir, opts);
  }
  return OpenEngine(dir, durability, compress_pages);
}

inline bool IsNotFound(const Status& st) {
  return st.code() == StatusCode::kNotFound;
}

// LSV powerfail: correct durability defaults; optional production background
// (SnapshotFairRwLock defers background exclusive while snapshot pins active).
inline EngineOptions LsvPowerfailOptions(const std::string& dir,
                                         DurabilityClass durability,
                                         bool production_background = false) {
  EngineOptions opts;
  switch (durability) {
    case DurabilityClass::kSync:
      opts = EngineOptions::EnterpriseDefaults(dir);
      break;
    case DurabilityClass::kGroup:
      opts = EngineOptions::BenchmarkGroupDefaults(dir);
      break;
    default:
      opts = EngineOptions::StandardDefaults(dir);
      break;
  }
  opts.path = dir;
  opts.durability = durability;
  if (!production_background) {
    opts.background_flush = false;
    opts.background_summary_validate = false;
  }
  return opts;
}

inline std::unique_ptr<Engine> OpenLsvPowerfailEngine(
    const std::string& dir, DurabilityClass durability,
    bool production_background = false) {
  return OpenEngineWithOptions(
      dir, LsvPowerfailOptions(dir, durability, production_background));
}

}  // namespace test
}  // namespace ebtree
