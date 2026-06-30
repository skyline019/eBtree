#pragma once

#include "powerfail_fuzz.h"
#include "rar_types.h"

#include "ebtree/engine/shard_router.h"

namespace ebtree {
namespace test {

inline audit::ExpectSnapshot BuildExpectFromOracle(
    const CommittedOracle& oracle,
    const std::unordered_map<std::string, std::string>& visibility_snap,
    audit::ContractMode mode, uint32_t shard_count) {
  audit::ExpectSnapshot expect{};
  expect.mode = mode;

  const auto& durable = oracle.durable_kv();
  const auto& touched = oracle.touched_keys();

  if (mode == audit::ContractMode::kDurable) {
    for (const auto& entry : durable) {
      audit::ExpectedKeyEntry e{};
      e.shard = RouteShard(entry.first, shard_count);
      e.key = entry.first;
      e.value = entry.second;
      e.in_durable_set = true;
      e.in_visibility_set = visibility_snap.count(entry.first) > 0;
      expect.entries.push_back(std::move(e));
    }
  } else {
    for (const auto& entry : visibility_snap) {
      audit::ExpectedKeyEntry e{};
      e.shard = RouteShard(entry.first, shard_count);
      e.key = entry.first;
      e.value = entry.second;
      e.in_visibility_set = true;
      e.in_durable_set = durable.count(entry.first) > 0;
      expect.entries.push_back(std::move(e));
    }
  }

  expect.touched_keys.assign(touched.begin(), touched.end());
  return expect;
}

inline std::vector<std::string> CollectProbeKeysFromExpect(
    const audit::ExpectSnapshot& expect) {
  return expect.touched_keys;
}

}  // namespace test
}  // namespace ebtree
