#include <cstdlib>
#include <fstream>
#include <iostream>
#include <string>

#include "json_writer.h"
#include "catalog_expect.h"
#include "op_log_expect.h"
#include "rar_builder.h"
#include "rar_chain.h"
#include "rar_chain_anchor.h"
#include "rar_merkle.h"
#include "rar_sign.h"
#include "rar_types.h"
#include "shard_paths.h"
#include "sidecar_stats.h"

#include "ebtree/common/status.h"

namespace {

void PrintUsage() {
  std::cerr
      << "Usage:\n"
      << "  ebtree_audit physical --path <dir> [--output <file>]\n"
      << "  ebtree_audit attest --path <dir> [--tier balanced|sync|group]\n"
      << "         [--output <file>] [--shard-count N]\n"
      << "  ebtree_audit verify --path <dir> [--expect <file>|--op-log <file>|--catalog <file>]\n"
      << "         [--mode durable|visibility] [--tier balanced|sync|group]\n"
      << "         [--max-missing N] [--output <file>] [--require-signature]\n"
      << "  ebtree_audit sign --input <file> --key <seed|file> [--output <file>]\n"
      << "  ebtree_audit verify-sig --input <file> --key <pubkey|secret> [--pubkey <file>]\n"
      << "  ebtree_audit chain-verify --path <dir> [--chain <file>] [--require-signature]\n"
      << "         [--require-anchor] [--anchor-dir <dir>]\n"
      << "  ebtree_audit chain-anchor --path <dir> [--chain <file>] [--anchor-dir <dir>]\n"
      << "         [--publish]\n"
      << "  ebtree_audit chain-proof --path <dir> [--chain <file>] --seq <N>\n";
}

bool WriteOutput(const std::string& path, const std::string& content) {
  if (path.empty() || path == "-") {
    std::cout << content;
    return true;
  }
  std::ofstream out(path);
  if (!out) {
    std::cerr << "failed to open output: " << path << "\n";
    return false;
  }
  out << content;
  return true;
}

std::string ReadFile(const std::string& path) {
  std::ifstream in(path);
  return std::string((std::istreambuf_iterator<char>(in)),
                     std::istreambuf_iterator<char>());
}

std::string GetArg(int argc, char** argv, const std::string& flag,
                   const std::string& default_value = "") {
  for (int i = 1; i + 1 < argc; ++i) {
    if (argv[i] == flag) return argv[i + 1];
  }
  return default_value;
}

}  // namespace

int main(int argc, char** argv) {
  if (argc < 2) {
    PrintUsage();
    return 2;
  }

  const std::string command = argv[1];
  const std::string path = GetArg(argc, argv, "--path");
  const std::string output = GetArg(argc, argv, "--output", "-");

  using namespace ebtree::audit;

  if (command == "sign") {
    const std::string input = GetArg(argc, argv, "--input");
    const std::string key = GetArg(argc, argv, "--key");
    if (input.empty() || key.empty()) {
      PrintUsage();
      return 2;
    }
    const std::string body = ReadFile(input);
    std::string sig;
    if (!SignRarJson(body, key, &sig).ok()) return 2;
    std::string out_json = body;
    if (out_json.find("\"signature\"") == std::string::npos) {
      const auto pos = out_json.rfind('}');
      if (pos != std::string::npos) {
        out_json.insert(pos, ",\n  \"signature\": \"" + sig + "\"");
      }
    }
    if (!WriteOutput(output, out_json)) return 2;
    return 0;
  }

  if (command == "verify-sig") {
    const std::string input = GetArg(argc, argv, "--input");
    const std::string key = GetArg(argc, argv, "--key");
    const std::string pubkey = GetArg(argc, argv, "--pubkey", key);
    if (input.empty() || pubkey.empty()) {
      PrintUsage();
      return 2;
    }
    const std::string body = ReadFile(input);
    const auto pos = body.find("\"signature\": \"");
    if (pos == std::string::npos) return 1;
    const auto start = pos + 14;
    const auto end = body.find('"', start);
    const std::string sig = body.substr(start, end - start);
    if (!VerifyRarSignature(body, sig, pubkey).ok()) return 1;
    return 0;
  }

  if (path.empty()) {
    PrintUsage();
    return 2;
  }

  if (command == "physical") {
    RarReport report{};
    const ebtree::Status st = BuildPhysicalOnly(path, &report);
    if (!st.ok()) {
      std::cerr << st.message() << "\n";
      return 2;
    }
    report.engine_path = path;
    report.shard_count = DiscoverShardCount(path);
    if (!WriteOutput(output, RarReportToJson(report))) return 2;
    return 0;
  }

  if (command == "attest") {
    BuildRarOptions opts{};
    opts.engine_path = path;
    opts.durability_tier =
        DurabilityClassFromString(GetArg(argc, argv, "--tier", "balanced"));
    const std::string shard_count_str = GetArg(argc, argv, "--shard-count");
    if (!shard_count_str.empty()) {
      opts.shard_count = static_cast<uint32_t>(std::stoul(shard_count_str));
    }
    opts.op_log_path = path + "/ebtree.op_log.jsonl";
    opts.catalog_path = path + "/ebtree.catalog.json";

    RarReport report{};
    const ebtree::Status st = BuildRar(opts, &report);
    if (!st.ok()) {
      std::cerr << st.message() << "\n";
      return 2;
    }
    if (!WriteOutput(output, RarReportToJson(report))) return 2;
    if (GetArg(argc, argv, "--require-signature") == "1" ||
        GetArg(argc, argv, "--require-signature") == "true") {
      if (RarReportToJson(report).find("\"signature\"") == std::string::npos) {
        return 1;
      }
    }
    if (report.verdict == RarVerdict::kRefuseStart) return 1;
    return 0;
  }

  if (command == "verify") {
    const std::string expect_path = GetArg(argc, argv, "--expect");
    const std::string op_log_path = GetArg(argc, argv, "--op-log");
    const std::string catalog_path = GetArg(argc, argv, "--catalog");
    const std::string mode_str = GetArg(argc, argv, "--mode", "durable");
    const std::string tier = GetArg(argc, argv, "--tier", "balanced");
    const std::string max_missing = GetArg(argc, argv, "--max-missing", "0");
    const std::string require_sig = GetArg(argc, argv, "--require-signature");

    if (expect_path.empty() && op_log_path.empty() && catalog_path.empty()) {
      PrintUsage();
      return 2;
    }

    BuildRarOptions opts{};
    opts.engine_path = path;
    opts.durability_tier = DurabilityClassFromString(tier);
    opts.policy.recovery_max_missing = static_cast<uint64_t>(std::stoull(max_missing));
    opts.op_log_path = op_log_path.empty() ? path + "/ebtree.op_log.jsonl" : op_log_path;
    opts.catalog_path =
        catalog_path.empty() ? path + "/ebtree.catalog.json" : catalog_path;

    ExpectSnapshot expect{};
    const ContractMode mode =
        mode_str == "visibility" ? ContractMode::kVisibility
                                 : ContractMode::kDurable;

    if (!op_log_path.empty()) {
      const ebtree::Status ls =
          LoadOpLogExpectSnapshot(op_log_path, mode, &expect);
      if (!ls.ok()) {
        std::cerr << ls.message() << "\n";
        return 2;
      }
    } else if (!expect_path.empty()) {
      const ebtree::Status es = LoadExpectFromJson(expect_path, mode, &expect);
      if (!es.ok()) {
        std::cerr << es.message() << "\n";
        return 2;
      }
    }

    if (!catalog_path.empty()) {
      const ebtree::Status cs = LoadCatalogExpectSnapshot(catalog_path, &expect);
      if (!cs.ok()) {
        std::cerr << cs.message() << "\n";
        return 2;
      }
    }

    if (!expect.entries.empty() || !expect.touched_keys.empty()) {
      opts.expect = &expect;
    }

    RarReport report{};
    const ebtree::Status st = BuildRar(opts, &report);
    if (!st.ok()) {
      std::cerr << st.message() << "\n";
      return 2;
    }
    if (!WriteOutput(output, RarReportToJson(report))) return 2;
    if (require_sig == "1" || require_sig == "true") {
      const std::string json = RarReportToJson(report);
      if (json.find("\"signature\"") == std::string::npos) return 1;
    }
    if (report.verdict == RarVerdict::kRefuseStart) return 1;
    return 0;
  }

  if (command == "chain-verify") {
    const std::string chain_path =
        GetArg(argc, argv, "--chain",
               path + "/ebtree.rar.chain.jsonl");
    RarChainVerifyReport report{};
    const ebtree::Status st = VerifyRarChain(chain_path, &report);
    if (!st.ok()) {
      std::cerr << st.message() << "\n";
      return 2;
    }
    std::cout << "entries=" << report.entry_count
              << " last_sequence=" << report.last_sequence
              << " consistent=" << (report.consistent ? "true" : "false")
              << "\n";
    for (const auto& err : report.errors) {
      std::cerr << err << "\n";
    }
    if (GetArg(argc, argv, "--require-signature") == "1" ||
        GetArg(argc, argv, "--require-signature") == "true") {
      std::vector<RarChainEntry> entries;
      if (!ReadRarChainEntries(chain_path, &entries).ok()) return 2;
      for (const auto& entry : entries) {
        if (entry.signature.empty()) return 1;
      }
    }
    if (GetArg(argc, argv, "--require-anchor") == "1" ||
        GetArg(argc, argv, "--require-anchor") == "true") {
      const std::string anchor_dir =
          GetArg(argc, argv, "--anchor-dir", DefaultCarlAnchorDir(path));
      const bool require_anchor_sig =
          GetArg(argc, argv, "--require-signature") == "1" ||
          GetArg(argc, argv, "--require-signature") == "true";
      const ebtree::Status av =
          VerifyCarlAnchorRequired(chain_path, anchor_dir, require_anchor_sig);
      if (!av.ok()) {
        std::cerr << "anchor verify failed: " << av.message() << "\n";
        return 1;
      }
      std::cout << "anchor=ok\n";
    }
    return report.consistent ? 0 : 1;
  }

  if (command == "chain-anchor") {
    const std::string chain_path =
        GetArg(argc, argv, "--chain",
               path + "/ebtree.rar.chain.jsonl");
    const std::string anchor_dir =
        GetArg(argc, argv, "--anchor-dir", DefaultCarlAnchorDir(path));
    if (GetArg(argc, argv, "--publish") == "1" ||
        GetArg(argc, argv, "--publish") == "true") {
      CarlSignedTreeHead sth{};
      const ebtree::Status ps = PublishCarlAnchor(chain_path, anchor_dir, &sth);
      if (!ps.ok()) {
        std::cerr << ps.message() << "\n";
        return 2;
      }
      std::cout << "published sequence=" << sth.chain_sequence
                << " root_hash=" << sth.root_hash << "\n";
      return 0;
    }
    PrintUsage();
    return 2;
  }

  if (command == "chain-proof") {
    const std::string chain_path =
        GetArg(argc, argv, "--chain",
               path + "/ebtree.rar.chain.jsonl");
    const std::string seq_str = GetArg(argc, argv, "--seq");
    if (seq_str.empty()) {
      PrintUsage();
      return 2;
    }
    const uint64_t seq = static_cast<uint64_t>(std::stoull(seq_str));
    std::vector<std::string> proof;
    std::string root;
    const ebtree::Status ps =
        GenerateCarlMerkleProof(CarlMerkleSidecarPath(chain_path), seq, &proof,
                                &root);
    if (!ps.ok()) {
      std::cerr << ps.message() << "\n";
      return 2;
    }
    std::cout << "root=" << root << " proof_steps=" << proof.size() << "\n";
    for (size_t i = 0; i < proof.size(); ++i) {
      std::cout << "proof[" << i << "]=" << proof[i] << "\n";
    }
    return 0;
  }

  PrintUsage();
  return 2;
}
