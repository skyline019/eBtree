#include "op_log_head_hash.h"

#include <fstream>

#include "digest.h"

namespace ebtree {
namespace audit {

Status OpLogHeadSha256(const std::string& op_log_path, std::string* out_hex) {
  if (!out_hex) return Status::InvalidArgument("out_hex is null");
  out_hex->clear();
  std::ifstream in(op_log_path);
  if (!in) return Status::NotFound("op_log not found: " + op_log_path);

  std::string line;
  std::string last;
  while (std::getline(in, line)) {
    if (!line.empty()) last = line;
  }
  if (last.empty()) return Status::Ok();
  *out_hex = Sha256HexString(last);
  return Status::Ok();
}

}  // namespace audit
}  // namespace ebtree
