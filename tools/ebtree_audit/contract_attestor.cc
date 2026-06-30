#include "contract_attestor.h"

#include <unordered_map>

#include "digest.h"

namespace ebtree {
namespace audit {

namespace {

const KeyProbeResult* FindProbe(const RecoveryReport& recovery,
                                const std::string& key) {
  for (const auto& probe : recovery.probes) {
    if (probe.key == key) return &probe;
  }
  return nullptr;
}

}  // namespace

Status ContractAttest(const ExpectSnapshot& expect,
                      const RecoveryReport& recovery, ContractReport* out) {
  if (!out) return Status::InvalidArgument("out is null");

  out->mode = expect.mode;
  out->key_set_source =
      expect.key_set_source.empty() ? "oracle" : expect.key_set_source;
  out->missing.clear();
  out->unexpected.clear();
  out->pending_uncommitted.clear();

  std::unordered_map<std::string, ExpectedKeyEntry> expected_map;
  for (const auto& entry : expect.entries) {
    expected_map[entry.key] = entry;
  }

  uint64_t expected_count = 0;
  uint64_t recovered_count = 0;

  for (const auto& entry : expect.entries) {
    const bool should_expect =
        (expect.mode == ContractMode::kDurable) ? entry.in_durable_set
                                                : entry.in_visibility_set;
    if (!should_expect) continue;
    ++expected_count;

    const KeyProbeResult* probe = FindProbe(recovery, entry.key);
    const std::string expected_hash =
        !entry.expected_value_hash.empty()
            ? entry.expected_value_hash
            : Sha256HexString(entry.value);
    if (!probe || !probe->found) {
      ContractKeyIssue issue{};
      issue.shard = entry.shard;
      issue.key = entry.key;
      issue.expected_hash = expected_hash;
      out->missing.push_back(std::move(issue));
      continue;
    }
    ++recovered_count;
    if (probe->value_sha256 != expected_hash) {
      ContractKeyIssue issue{};
      issue.shard = entry.shard;
      issue.key = entry.key;
      issue.expected_hash = expected_hash;
      issue.actual_hash = probe->value_sha256;
      out->missing.push_back(std::move(issue));
    }
  }

  for (const auto& key : expect.touched_keys) {
    const KeyProbeResult* probe = FindProbe(recovery, key);
    const bool in_expected = expected_map.count(key) > 0;
    const bool should_be_absent =
        in_expected &&
        ((expect.mode == ContractMode::kDurable &&
          !expected_map.at(key).in_durable_set &&
          !expected_map.at(key).in_visibility_set) ||
         (expect.mode == ContractMode::kVisibility &&
          !expected_map.at(key).in_visibility_set));

    if (probe && probe->found && should_be_absent) {
      ContractKeyIssue issue{};
      issue.key = key;
      issue.actual_hash = probe->value_sha256;
      out->unexpected.push_back(std::move(issue));
    } else if (probe && probe->found && !in_expected) {
      ContractKeyIssue issue{};
      issue.key = key;
      issue.actual_hash = probe->value_sha256;
      out->unexpected.push_back(std::move(issue));
    }
  }

  for (const auto& entry : expect.entries) {
    if (entry.in_durable_set) continue;
    if (!entry.in_visibility_set && expect.mode == ContractMode::kVisibility) {
      continue;
    }
    const KeyProbeResult* probe = FindProbe(recovery, entry.key);
    if (probe && probe->found) {
      ContractKeyIssue issue{};
      issue.shard = entry.shard;
      issue.key = entry.key;
      issue.actual_hash = probe->value_sha256;
      out->pending_uncommitted.push_back(std::move(issue));
    }
  }

  out->durable_expected_count = expected_count;
  out->recovered_count = recovered_count;
  return Status::Ok();
}

}  // namespace audit
}  // namespace ebtree
