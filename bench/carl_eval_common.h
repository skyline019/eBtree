#pragma once

#include <cstdint>
#include <string>

namespace ebtree {
namespace bench {
namespace carl_eval {

struct Write100kResult {
  double ops_per_sec{0.0};
  int64_t elapsed_ms{0};
};

struct VerifyLatencyResult {
  int64_t verify_ms{0};
  uint64_t entry_count{0};
};

struct AnchorLatencyResult {
  int64_t publish_ms{0};
};

struct LazyScanResult {
  int64_t scan_ms{0};
  uint64_t row_count{0};
};

Write100kResult RunNoCarlWrite100k(const std::string& dir);
Write100kResult RunCarlMonitorWrite100k(const std::string& dir);
VerifyLatencyResult MeasureChainVerifyLatency(const std::string& chain_path,
                                              uint64_t entry_count);
AnchorLatencyResult MeasureAnchorPublishLatency(const std::string& chain_path,
                                                const std::string& anchor_dir);

LazyScanResult MeasureLazyScan10k(const std::string& dir);

void PrintEvalMarkdown(const Write100kResult& no_carl,
                       const Write100kResult& carl,
                       const VerifyLatencyResult& verify_1k,
                       const AnchorLatencyResult& anchor,
                       const LazyScanResult& lazy_scan);

}  // namespace carl_eval
}  // namespace bench
}  // namespace ebtree
