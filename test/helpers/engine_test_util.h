#pragma once

#include <cstdlib>
#include <filesystem>
#include <memory>
#include <string>

#include "ebtree/engine/engine.h"

namespace ebtree {
namespace test {

inline std::filesystem::path TestTmpRoot() {
  if (const char* env = std::getenv("EBTREE_TEST_TMP_ROOT")) {
    if (env[0] != '\0') {
      return std::filesystem::path(env);
    }
  }
  return std::filesystem::temp_directory_path() / "ebtree_test";
}

inline std::string TempDir(const std::string& name) {
  const auto base = TestTmpRoot() / name;
  std::filesystem::remove_all(base);
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

inline std::unique_ptr<Engine> OpenEngine(
    const std::string& dir,
    DurabilityClass durability = DurabilityClass::kBalanced,
    bool compress_pages = false) {
  if (durability == DurabilityClass::kBalanced) {
    EngineOptions opts = EngineOptions::ProductionDefaults(dir);
    opts.background_summary_validate = false;
    opts.background_flush = false;
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

inline bool IsNotFound(const Status& st) {
  return st.code() == StatusCode::kNotFound;
}

}  // namespace test
}  // namespace ebtree
