#include <gtest/gtest.h>

#include "engine_test_util.h"
#include "ebtree/concept/wal/wal_segment.h"

namespace ebtree {
namespace test {

TEST(SnapshotOccEngine, CrossEngineCurrentLsnAdvancesAfterOtherCommit) {
  const std::string dir = TempDir("occ_cross_engine");
  EngineOptions opts = EngineOptions::StandardDefaults(dir);
  std::unique_ptr<Engine> seed;
  ASSERT_TRUE(Engine::Open(opts, &seed).ok());
  ASSERT_TRUE(seed->Put("row", "v0", 0).ok());
  ASSERT_TRUE(seed->Checkpoint().ok());

  std::unique_ptr<Engine> writer;
  std::unique_ptr<Engine> other;
  ASSERT_TRUE(Engine::Open(opts, &writer).ok());
  ASSERT_TRUE(Engine::Open(opts, &other).ok());

  const SnapshotToken snap = writer->CaptureSnapshot();
  ASSERT_TRUE(writer->AppendTxnBegin(10, snap).ok());
  writer->PinSnapshot(snap);

  uint64_t ticket = 0;
  ASSERT_TRUE(writer->ResolveLsnAtSnapshot("row", snap, 10, &ticket).ok());

  ASSERT_TRUE(other->AppendTxnBegin(11, other->CaptureSnapshot()).ok());
  ASSERT_TRUE(other->Put("row", "v1", 11).ok());
  ASSERT_TRUE(other->AppendTxnCommit(11).ok());
  other->memtable()->PromoteTxn(11);

  const uint64_t wal_cutoff = writer->loaded_superblock().critical.wal_lsn;
  ASSERT_TRUE(other->wal());
  const uint64_t disk_max =
      WalSegmentReplayer::MaxLsnOnDisk(other->wal()->path(), wal_cutoff);
  EXPECT_GT(disk_max, wal_cutoff);
  const uint64_t wal_key_lsn = WalSegmentReplayer::LatestCommittedLsnForKey(
      other->wal()->path(), "row", wal_cutoff);
  EXPECT_GT(wal_key_lsn, ticket);

  uint64_t current = 0;
  ASSERT_TRUE(writer->ResolveCurrentCommittedLsn("row", &current).ok());
  EXPECT_GT(current, ticket);
}

}  // namespace test
}  // namespace ebtree
