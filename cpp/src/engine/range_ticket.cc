#include "ebtree/engine/range_ticket.h"

#include <algorithm>
#include <memory>
#include <mutex>
#include <unordered_map>

namespace ebtree {

namespace {

bool BetweenInclusive(const std::string& key, const std::string& lo,
                      const std::string& hi) {
  if (!lo.empty() && key < lo) return false;
  if (!hi.empty() && key > hi) return false;
  return true;
}

}  // namespace

RangeTicketRegistry& RangeTicketRegistry::ForPath(
    const std::string& engine_path) {
  static std::mutex map_mu;
  static std::unordered_map<std::string, std::shared_ptr<RangeTicketRegistry>>
      by_path;
  std::lock_guard<std::mutex> lock(map_mu);
  std::shared_ptr<RangeTicketRegistry>& slot = by_path[engine_path];
  if (!slot) {
    slot = std::make_shared<RangeTicketRegistry>();
  }
  return *slot;
}

void RangeTicketRegistry::Register(uint32_t txn_id, uint64_t snapshot_lsn,
                                   std::string key_lo, std::string key_hi) {
  std::lock_guard<std::mutex> lock(mu_);
  tickets_.push_back(
      ActiveRangeTicket{txn_id, snapshot_lsn, std::move(key_lo), std::move(key_hi)});
}

void RangeTicketRegistry::UnregisterTxn(uint32_t txn_id) {
  std::lock_guard<std::mutex> lock(mu_);
  tickets_.erase(std::remove_if(tickets_.begin(), tickets_.end(),
                                [txn_id](const ActiveRangeTicket& t) {
                                  return t.txn_id == txn_id;
                                }),
                 tickets_.end());
}

bool RangeTicketRegistry::KeyInRange(const std::string& key,
                                     const std::string& lo,
                                     const std::string& hi) {
  return BetweenInclusive(key, lo, hi);
}

bool RangeTicketRegistry::RangesOverlap(const std::string& a_lo,
                                        const std::string& a_hi,
                                        const std::string& b_lo,
                                        const std::string& b_hi) {
  const std::string& a_start = a_lo.empty() ? b_lo : a_lo;
  const std::string& b_start = b_lo.empty() ? a_lo : b_lo;
  if (a_hi.empty() || b_hi.empty()) return true;
  return !(a_hi < b_start || b_hi < a_start);
}

Status RangeTicketRegistry::ValidateCommit(
    uint32_t txn_id, uint64_t /*snapshot_lsn*/, const std::string& /*key_lo*/,
    const std::string& /*key_hi*/,
    const std::unordered_map<std::string, uint64_t>& write_set) const {
  std::lock_guard<std::mutex> lock(mu_);
  for (const auto& ticket : tickets_) {
    if (ticket.txn_id == txn_id) continue;
    for (const auto& w : write_set) {
      if (!KeyInRange(w.first, ticket.key_lo, ticket.key_hi)) continue;
      if (w.second == 0) {
        return Status::Conflict("phantom range conflict");
      }
    }
  }
  return Status::Ok();
}

}  // namespace ebtree
