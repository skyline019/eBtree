#include <gtest_capi.h>

#include <cstring>
#include <iostream>
#include <string>
#include <vector>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

#include "test_suites.inc"

namespace {

struct Options {
  std::string suite_filter = "all";
  std::string gtest_filter;
  bool list = false;
};

Options ParseArgs(int argc, char** argv) {
  Options opt;
  for (int i = 1; i < argc; ++i) {
    const char* arg = argv[i];
    if (std::strncmp(arg, "--suite=", 8) == 0) {
      opt.suite_filter = arg + 8;
    } else if (std::strncmp(arg, "--gtest_filter=", 15) == 0) {
      opt.gtest_filter = arg + 15;
    } else if (std::strcmp(arg, "--list") == 0) {
      opt.list = true;
    } else if (std::strncmp(arg, "--category=", 11) == 0) {
      opt.suite_filter = arg + 11;
    }
  }
  return opt;
}

std::vector<std::string> SplitFilterTokens(const std::string& filter) {
  std::vector<std::string> tokens;
  if (filter.empty()) return tokens;
  size_t start = 0;
  while (start < filter.size()) {
    const size_t comma = filter.find(',', start);
    std::string token = filter.substr(
        start, comma == std::string::npos ? std::string::npos : comma - start);
    if (!token.empty()) tokens.push_back(token);
    if (comma == std::string::npos) break;
    start = comma + 1;
  }
  if (tokens.empty() && !filter.empty()) tokens.push_back(filter);
  return tokens;
}

const char* MatrixSubGtestFilter(const std::string& sub) {
  if (sub == "recovery") {
    return "RecoveryMatrixTest.*:EbMatrixSchema.RecoveryCasesNonEmpty";
  }
  if (sub == "no_fallback") {
    return "NoFallbackMatrixTest.*:EbMatrixSchema.NoFallbackCasesNonEmpty";
  }
  if (sub == "pipeline") {
    return "PipelineMatrixTest.*:EbMatrixSchema.PipelineCasesNonEmpty";
  }
  if (sub == "shard") {
    return "ShardMatrixTest.*:EbMatrixSchema.ShardCasesNonEmpty";
  }
  if (sub == "chaos") {
    return "ChaosMatrixTest.*:EbMatrixSchema.ChaosCasesNonEmpty";
  }
  if (sub == "paged") {
    return "PagedMatrixTest.*:EbMatrixSchema.PagedCasesNonEmpty";
  }
  if (sub == "concurrent") {
    return "ConcurrentMatrixTest.*:EbMatrixSchema.ConcurrentCasesNonEmpty";
  }
  if (sub == "flashback") {
    return "FlashbackMatrixTest.*:EbMatrixSchema.FlashbackCasesNonEmpty";
  }
  if (sub == "balanced") {
    return "BalancedMatrixTest.*:EbMatrixSchema.BalancedCasesNonEmpty";
  }
  if (sub == "ondisk") {
    return "OndiskMatrixTest.*:EbMatrixSchema.OndiskCasesNonEmpty";
  }
  if (sub == "wal_index") {
    return "WalIndexMatrixTest.*:EbMatrixSchema.WalIndexCasesNonEmpty";
  }
  return nullptr;
}

std::string MatrixGtestFilterForSuiteFilter(const std::string& filter) {
  const auto tokens = SplitFilterTokens(filter);
  bool wants_all = false;
  std::vector<std::string> parts;
  for (const auto& token : tokens) {
    if (token == "matrix") {
      wants_all = true;
      continue;
    }
    if (token.rfind("matrix/", 0) != 0) continue;
    const std::string sub = token.substr(7);
    const char* part = MatrixSubGtestFilter(sub);
    if (part) parts.emplace_back(part);
  }
  if (wants_all || parts.empty()) return {};
  std::string combined;
  for (size_t i = 0; i < parts.size(); ++i) {
    if (i > 0) combined.push_back(':');
    combined += parts[i];
  }
  return combined;
}

bool SuiteMatchesSingle(const EbTestSuiteEntry& entry, const std::string& token) {
  if (token.empty() || token == "all") return true;
  const std::string name(entry.name);
  const std::string cats(entry.categories ? entry.categories : "");
  if (name == token) return true;
  if (cats.find(token) != std::string::npos) return true;
  if (name == "matrix" && token.rfind("matrix/", 0) == 0) return true;
  return false;
}

bool SuiteMatches(const EbTestSuiteEntry& entry, const std::string& filter) {
  if (filter == "all") return true;
  const auto tokens = SplitFilterTokens(filter);
  if (tokens.size() > 1) {
    for (const auto& token : tokens) {
      if (SuiteMatchesSingle(entry, token)) return true;
    }
    return false;
  }
  return SuiteMatchesSingle(entry, filter);
}

#ifdef _WIN32
std::string RunnerDirectory() {
  char buf[MAX_PATH]{};
  GetModuleFileNameA(nullptr, buf, MAX_PATH);
  std::string path(buf);
  const size_t slash = path.find_last_of("\\/");
  if (slash != std::string::npos) path.resize(slash);
  return path;
}

std::string SuiteDllPath(const EbTestSuiteEntry& entry) {
  return RunnerDirectory() + "\\tests\\" + entry.dll_basename + ".dll";
}

using RunFn = int (*)();
using InitFn = int (*)(int, const char* const*);

int RunDll(const std::string& path, const Options& opt) {
  HMODULE mod = LoadLibraryA(path.c_str());
  if (!mod) {
    std::cerr << "LoadLibrary failed: " << path << " err=" << GetLastError()
              << '\n';
    return 1;
  }
  auto init = reinterpret_cast<InitFn>(
      GetProcAddress(mod, "gtest_capi_init_from_argv"));
  auto run = reinterpret_cast<RunFn>(GetProcAddress(mod, "gtest_capi_run_all"));
  auto set_filter = reinterpret_cast<int (*)(const char*)>(
      GetProcAddress(mod, "gtest_capi_set_filter"));
  auto failed_count = reinterpret_cast<int (*)()>(
      GetProcAddress(mod, "gtest_capi_failed_test_count"));
  if (!init || !run) {
    std::cerr << "missing gtest_capi exports in " << path << '\n';
    FreeLibrary(mod);
    return 1;
  }
  const char* argv0[] = {"ebtree_test_runner"};
  if (init(1, argv0) != 0) {
    FreeLibrary(mod);
    return 1;
  }
  if (!opt.gtest_filter.empty() && set_filter) {
    set_filter(opt.gtest_filter.c_str());
  }
  const int rc = run();
  const int fails = failed_count ? failed_count() : rc;
  FreeLibrary(mod);
  return fails != 0 ? 1 : 0;
}
#endif

}  // namespace

int main(int argc, char** argv) {
  const Options opt = ParseArgs(argc, argv);
  if (opt.list) {
    for (int i = 0; i < kEbTestSuiteCount; ++i) {
      std::cout << kEbTestSuites[i].name << " ("
                << kEbTestSuites[i].dll_basename
                << ") cats=" << kEbTestSuites[i].categories << '\n';
    }
    return 0;
  }

#ifndef _WIN32
  std::cerr << "ebtree_test_runner DLL loading is Windows-only in this MVP\n";
  return 1;
#else
  const std::string matrix_filter = MatrixGtestFilterForSuiteFilter(opt.suite_filter);
  int failures = 0;
  for (int i = 0; i < kEbTestSuiteCount; ++i) {
    const auto& entry = kEbTestSuites[i];
    if (!SuiteMatches(entry, opt.suite_filter)) continue;
    Options run_opt = opt;
    if (std::string(entry.name) == "matrix" && !matrix_filter.empty()) {
      if (run_opt.gtest_filter.empty()) {
        run_opt.gtest_filter = matrix_filter;
      } else {
        run_opt.gtest_filter += ":" + matrix_filter;
      }
    }
    const std::string dll = SuiteDllPath(entry);
    std::cout << "== suite " << entry.name << " (" << dll << ") ==\n";
    failures += RunDll(dll, run_opt);
  }
  return failures > 0 ? 1 : 0;
#endif
}
