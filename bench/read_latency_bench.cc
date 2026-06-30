#include <algorithm>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include "ebtree/engine/engine.h"

namespace {

void PrintLatencyLine(const char* label, int keys,
                      const std::vector<int64_t>& latencies_ns) {
  std::vector<int64_t> sorted = latencies_ns;
  std::sort(sorted.begin(), sorted.end());
  const int64_t p50 = sorted[sorted.size() / 2];
  const int64_t p99 = sorted[(sorted.size() * 99) / 100];
  std::printf("%s gets=%d p50_ns=%lld p99_ns=%lld p50_us=%.1f p99_us=%.1f\n",
              label, keys, static_cast<long long>(p50),
              static_cast<long long>(p99), static_cast<double>(p50) / 1000.0,
              static_cast<double>(p99) / 1000.0);
}

std::vector<int64_t> RunGets(ebtree::Engine* engine, int keys) {
  std::vector<int64_t> latencies_ns;
  latencies_ns.reserve(keys);
  for (int i = 0; i < keys; ++i) {
    const std::string key = "k" + std::to_string(i);
    std::string value;
    const auto start = std::chrono::steady_clock::now();
    if (!engine->Get(key, &value).ok()) {
      std::fprintf(stderr, "get failed at %d\n", i);
      std::exit(1);
    }
    const auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now() - start);
    latencies_ns.push_back(ns.count());
  }
  return latencies_ns;
}

}  // namespace

int main() {
  const auto dir =
      (std::filesystem::temp_directory_path() / "ebtree_bench_read").string();
  std::filesystem::remove_all(dir);
  std::filesystem::create_directories(dir);

  ebtree::EngineOptions opts;
  opts.path = dir;
  opts.durability = ebtree::DurabilityClass::kSync;
  opts.page_cache_capacity = 128;

  std::unique_ptr<ebtree::Engine> engine;
  if (!ebtree::Engine::Open(opts, &engine).ok()) {
    std::fprintf(stderr, "open failed\n");
    return 1;
  }

  constexpr int kKeys = 10000;
  for (int i = 0; i < kKeys; ++i) {
    const std::string key = "k" + std::to_string(i);
    if (!engine->Put(key, "payload").ok()) {
      std::fprintf(stderr, "put failed\n");
      return 1;
    }
  }
  (void)engine->Checkpoint();

  std::printf("read_bench durability=kSync page_cache=%zu\n",
              opts.page_cache_capacity);

  PrintLatencyLine("read_bench_warm", kKeys, RunGets(engine.get(), kKeys));

  engine.reset();
  if (!ebtree::Engine::Open(opts, &engine).ok()) {
    std::fprintf(stderr, "reopen failed\n");
    return 1;
  }
  PrintLatencyLine("read_bench_cold", kKeys, RunGets(engine.get(), kKeys));

  ebtree::EngineOptions lazy_opts = opts;
  lazy_opts.lazy_committed_load = true;
  engine.reset();
  if (!ebtree::Engine::Open(lazy_opts, &engine).ok()) {
    std::fprintf(stderr, "lazy reopen failed\n");
    return 1;
  }
  PrintLatencyLine("read_bench_cold_ondisk", kKeys, RunGets(engine.get(), kKeys));
  return 0;
}
