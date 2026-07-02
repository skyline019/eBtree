#pragma once

#include <cstdint>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include "ebtree/common/status.h"

namespace ebtree {

struct ActiveRangeTicket {
  uint32_t txn_id{0};
  uint64_t snapshot_lsn{0};
  std::string key_lo;
  std::string key_hi;
};

class RangeTicketRegistry {
 public:
  static RangeTicketRegistry& ForPath(const std::string& engine_path);

  void Register(uint32_t txn_id, uint64_t snapshot_lsn, std::string key_lo,
                std::string key_hi);
  void UnregisterTxn(uint32_t txn_id);
  Status ValidateCommit(uint32_t txn_id, uint64_t snapshot_lsn,
                        const std::string& key_lo, const std::string& key_hi,
                        const std::unordered_map<std::string, uint64_t>& write_set) const;

 private:
  static bool KeyInRange(const std::string& key, const std::string& lo,
                         const std::string& hi);
  static bool RangesOverlap(const std::string& a_lo, const std::string& a_hi,
                            const std::string& b_lo, const std::string& b_hi);

  mutable std::mutex mu_;
  std::vector<ActiveRangeTicket> tickets_;
};

}  // namespace ebtree
