#include "carl_eval_common.h"

#include <cstdio>
#include <filesystem>
#include <string>

int main(int argc, char** argv) {
  std::string dir = "carl_eval_bench_data";
  if (argc > 1) dir = argv[1];
  std::filesystem::create_directories(dir);

  const auto no_carl =
      ebtree::bench::carl_eval::RunNoCarlWrite100k(dir + "/no_carl");
  const auto carl =
      ebtree::bench::carl_eval::RunCarlMonitorWrite100k(dir + "/carl");
  const auto verify = ebtree::bench::carl_eval::MeasureChainVerifyLatency(
      dir + "/verify/chain.jsonl", 1000);
  const auto anchor = ebtree::bench::carl_eval::MeasureAnchorPublishLatency(
      dir + "/anchor/chain.jsonl", dir + "/anchor/anchors");
  const auto lazy =
      ebtree::bench::carl_eval::MeasureLazyScan10k(dir + "/lazy_scan");

  ebtree::bench::carl_eval::PrintEvalMarkdown(no_carl, carl, verify, anchor,
                                              lazy);
  return 0;
}
