#pragma once

#include <cstdint>
#include <fstream>
#include <mutex>
#include <string>

#include "ebtree/common/config.h"
#include "ebtree/common/status.h"

namespace ebtree {
namespace sql {

struct OpLogEntry {
  uint32_t version{1};
  std::string op;
  std::string key;
  std::string value_sha256;
  uint64_t lsn{0};
  bool durable_at_return{false};
  DurabilityClass tier{DurabilityClass::kBalanced};
  int64_t ts_unix{0};
};

class OpLogWriter {
 public:
  explicit OpLogWriter(std::string path);

  Status AppendPut(const std::string& key, const std::string& value_sha256,
                   uint64_t lsn, bool durable_at_return,
                   DurabilityClass tier);
  Status AppendDelete(const std::string& key, uint64_t lsn,
                      bool durable_at_return, DurabilityClass tier);
  Status MarkDurableThroughLsn(uint64_t lsn);
  Status Flush();

  const std::string& path() const { return path_; }

 private:
  Status AppendEntry(const OpLogEntry& entry);

  std::string path_;
  std::fstream stream_;
  std::mutex mu_;
};

}  // namespace sql
}  // namespace ebtree
