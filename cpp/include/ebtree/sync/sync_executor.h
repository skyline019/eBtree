#pragma once

#include <functional>
#include <vector>

#include "ebtree/common/status.h"

namespace ebtree {

class Engine;
class ShardEngine;

enum class SyncEventType {
  kWrite,
  kGroupCommit,
  kFlush,
  kSuperBlockCommit,
  kRecovery,
  kPrepare,
  kRead,
  kScan,
};

struct SyncContext {
  Engine* engine{nullptr};
  ShardEngine* shard{nullptr};
};

using SyncHandler = std::function<Status(SyncContext*)>;

class SyncExecutor {
 public:
  void Register(SyncEventType type, SyncHandler handler);
  Status Dispatch(SyncEventType type, SyncContext* ctx);

 private:
  std::vector<std::pair<SyncEventType, SyncHandler>> rules_;
};

void RegisterCoreSyncRules(SyncExecutor* exec);
void RegisterDurabilitySyncRules(SyncExecutor* exec);
void RegisterReadSyncRules(SyncExecutor* exec);
void RegisterRecoverySyncRules(SyncExecutor* exec);

}  // namespace ebtree
