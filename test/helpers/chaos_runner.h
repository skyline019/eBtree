#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>
#include <functional>
#include <random>
#include <string>
#include <thread>
#include <vector>

#include "ebtree/engine/engine.h"

namespace ebtree {
namespace test {

enum class ChaosOpType { kPut, kGet, kDelete };

enum class ChaosDestroyPoint {
  kNone,
  kAfterPut,
  kBeforeCheckpoint,
  kMidCheckpoint,
  kAfterFlush,
};

struct ChaosOp {
  ChaosOpType type{ChaosOpType::kPut};
  std::string key;
  std::string value;
};

inline std::vector<ChaosOp> GenerateChaosOps(uint32_t seed, size_t count) {
  std::mt19937 rng(seed);
  std::uniform_int_distribution<int> op_dist(0, 2);
  std::uniform_int_distribution<int> key_dist(0, 99);
  std::vector<ChaosOp> ops;
  ops.reserve(count);
  for (size_t i = 0; i < count; ++i) {
    ChaosOp op;
    op.type = static_cast<ChaosOpType>(op_dist(rng));
    op.key = "c" + std::to_string(key_dist(rng));
    op.value = "v" + std::to_string(static_cast<int>(i));
    ops.push_back(op);
  }
  return ops;
}

inline Status ExecuteChaosOps(Engine* engine, const std::vector<ChaosOp>& ops,
                              std::vector<ChaosOp>* applied) {
  if (!engine) return Status::InvalidArgument("null engine");
  for (const auto& op : ops) {
    Status st;
    switch (op.type) {
      case ChaosOpType::kPut:
        st = engine->Put(op.key, op.value);
        break;
      case ChaosOpType::kDelete:
        st = engine->Delete(op.key);
        break;
      case ChaosOpType::kGet: {
        std::string value;
        st = engine->Get(op.key, &value);
        if (st.code() == StatusCode::kNotFound) st = Status::Ok();
        break;
      }
    }
    if (!st.ok()) return st;
    if (applied && (op.type == ChaosOpType::kPut ||
                    op.type == ChaosOpType::kDelete)) {
      applied->push_back(op);
    }
  }
  return Status::Ok();
}

inline int ConcurrentReadWriteChaos(Engine* engine, uint32_t seed, int reader_threads,
                                    int writer_ops) {
  if (!engine) return -1;
  if (!engine->Put("chaos_anchor", "anchor").ok()) return -1;
  std::atomic<bool> stop{false};
  std::atomic<int> read_errors{0};
  std::vector<std::thread> readers;
  for (int t = 0; t < reader_threads; ++t) {
    readers.emplace_back([&]() {
      while (!stop.load(std::memory_order_acquire)) {
        std::string value;
        const Status st = engine->Get("chaos_anchor", &value);
        if (!st.ok()) ++read_errors;
        std::this_thread::yield();
      }
    });
  }
  const auto ops = GenerateChaosOps(seed, static_cast<size_t>(writer_ops));
  for (const auto& op : ops) {
    if (op.type == ChaosOpType::kPut) {
      if (!engine->Put(op.key, op.value).ok()) ++read_errors;
    } else if (op.type == ChaosOpType::kDelete) {
      if (!engine->Delete(op.key).ok()) ++read_errors;
    }
  }
  stop = true;
  for (auto& th : readers) th.join();
  return read_errors.load();
}

inline void DestroyEngineAt(Engine* engine, ChaosDestroyPoint point) {
  if (!engine || point == ChaosDestroyPoint::kNone) return;
  switch (point) {
    case ChaosDestroyPoint::kAfterPut:
      break;
    case ChaosDestroyPoint::kBeforeCheckpoint:
      break;
    case ChaosDestroyPoint::kMidCheckpoint:
      break;
    case ChaosDestroyPoint::kAfterFlush:
      (void)engine->Flush();
      break;
    default:
      break;
  }
}

}  // namespace test
}  // namespace ebtree
