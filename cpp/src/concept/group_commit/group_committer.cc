#include "ebtree/concept/group_commit/group_committer.h"

#include <algorithm>

namespace ebtree {

void GroupCommitter::RecordPut(uint64_t lsn, GroupCommitState* state) {
  if (!state) return;
  state->pending_stable_lsn = std::max(state->pending_stable_lsn, lsn);
  state->puts_since_commit++;
}

bool GroupCommitter::ShouldAutoCommit(const GroupCommitState& state,
                                      uint32_t batch_size) {
  return batch_size > 0 && state.puts_since_commit >= batch_size;
}

Status GroupCommitter::Commit(WalWriter* wal, EngineStats* stats,
                              GroupCommitState* state) {
  if (!wal || !stats || !state) {
    return Status::InvalidArgument("invalid group commit context");
  }
  if (state->pending_stable_lsn == 0) {
    return Status::Ok();
  }
  const Status fs = wal->Fsync();
  if (!fs.ok()) return fs;
  stats->stable_lsn = std::max(stats->stable_lsn, state->pending_stable_lsn);
  stats->group_commit_total++;
  state->puts_since_commit = 0;
  return Status::Ok();
}

}  // namespace ebtree
