#include <atomic>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "ebtree/engine/engine.h"
#include "ebtree/engine/shard_router.h"

namespace {

std::vector<std::vector<std::string>> KeysByShard(uint32_t shard_count,
                                                    int keys_per_shard) {
  std::vector<std::vector<std::string>> out(shard_count);
  for (int i = 0; static_cast<int>(out[0].size()) < keys_per_shard; ++i) {
    const std::string key = "mk" + std::to_string(i);
    const uint32_t id = ebtree::RouteShard(key, shard_count);
    if (static_cast<int>(out[id].size()) < keys_per_shard) {
      out[id].push_back(key);
    }
  }
  return out;
}

void RunMultishardBench(const char* label, ebtree::EngineOptions opts,
                        uint32_t shard_count, int keys_per_shard) {
  const auto dir = (std::filesystem::temp_directory_path() /
                    ("ebtree_bench_mwrite_" + std::string(label)))
                       .string();
  std::filesystem::remove_all(dir);
  std::filesystem::create_directories(dir);
  opts.path = dir;
  opts.shard_count = shard_count;
  opts.eager_shard_open = true;

  std::unique_ptr<ebtree::Engine> engine;
  if (!ebtree::Engine::Open(opts, &engine).ok()) {
    std::fprintf(stderr, "%s open failed\n", label);
    std::exit(1);
  }

  const auto keys_by_shard = KeysByShard(shard_count, keys_per_shard);
  const int total_ops = keys_per_shard * static_cast<int>(shard_count);

  const auto start = std::chrono::steady_clock::now();
  std::vector<std::thread> workers;
  workers.reserve(shard_count);
  for (uint32_t s = 0; s < shard_count; ++s) {
    workers.emplace_back([&, s]() {
      for (const auto& key : keys_by_shard[s]) {
        if (!engine->Put(key, "v").ok()) {
          std::fprintf(stderr, "%s put failed shard=%u\n", label, s);
          std::exit(1);
        }
      }
    });
  }
  for (auto& t : workers) t.join();
  if (opts.durability == ebtree::DurabilityClass::kGroup) {
    (void)engine->GroupCommit();
  }
  const auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(
      std::chrono::steady_clock::now() - start);
  const double sec = static_cast<double>(elapsed.count()) / 1e6;
  const double ops = static_cast<double>(total_ops) / sec;
  const double per_shard = ops / static_cast<double>(shard_count);
  std::printf(
      "%s shards=%u ops=%d elapsed_sec=%.3f ops_per_sec=%.0f per_shard_ops=%.0f\n",
      label, shard_count, total_ops, sec, ops, per_shard);
}

}  // namespace

int main(int argc, char** argv) {
  uint32_t shard_count = 1;
  bool ksync_mode = false;
  if (argc > 1) shard_count = static_cast<uint32_t>(std::stoul(argv[1]));
  if (argc > 2) ksync_mode = std::stoi(argv[2]) != 0;
  if (!ebtree::ValidateShardCount(shard_count).ok()) {
    std::fprintf(stderr, "shard_count must be 1, 4, 16, or 256\n");
    return 1;
  }

  constexpr int kKeysPerShard = 25000;
  ebtree::EngineOptions opts =
      ksync_mode ? ebtree::EngineOptions::EnterpriseDefaults("")
                 : ebtree::EngineOptions::BenchmarkGroupDefaults("");
  RunMultishardBench(ksync_mode ? "multishard_write_ksync"
                                : "multishard_write_kgroup",
                     opts, shard_count, kKeysPerShard);
  return 0;
}
