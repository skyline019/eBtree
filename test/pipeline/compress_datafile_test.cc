#include <gtest/gtest.h>

#include <string>

#include "engine_test_util.h"
#include "ebtree/concept/datafile/datafile.h"
#include "ebtree/engine/engine.h"

namespace ebtree {
namespace test {
namespace {

TEST(CompressDatafile, RoundTripWithCompression) {
  const std::string dir = TempDir("compress_datafile");
  DataFile df(dir + "/data.ebt");
  df.SetCompressValues(true);
  std::string value(256, 'z');
  value += std::string(128, 'a');
  ASSERT_TRUE(df.Append(1, "key1", value, false, 0).ok());
  ASSERT_TRUE(df.Append(2, "key2", "plain", false, 0).ok());

  std::string got;
  uint64_t lsn = 0;
  ASSERT_TRUE(df.ReadRecordAt(0, nullptr, &got, &lsn, nullptr, 0xFF).ok());
  EXPECT_EQ(got, value);
  EXPECT_EQ(lsn, 1u);
}

TEST(CompressDatafile, EnginePutScanCompressed) {
  const std::string dir = TempDir("compress_engine");
  EngineOptions opts{};
  opts.path = dir;
  opts.compress_values = true;
  std::unique_ptr<Engine> engine;
  ASSERT_TRUE(Engine::Open(opts, &engine).ok());
  std::string value(200, 'c');
  ASSERT_TRUE(engine->Put("row1", value).ok());
  ASSERT_TRUE(engine->Checkpoint().ok());
  std::string got;
  ASSERT_TRUE(engine->Get("row1", &got).ok());
  EXPECT_EQ(got, value);
}

TEST(CompressDatafile, EnginePutScanCompressedPages) {
  const std::string dir = TempDir("compress_pages_engine");
  EngineOptions opts{};
  opts.path = dir;
  opts.compress_pages = true;
  std::unique_ptr<Engine> engine;
  ASSERT_TRUE(Engine::Open(opts, &engine).ok());
  std::string value(512, 'p');
  ASSERT_TRUE(engine->Put("pg1", value).ok());
  ASSERT_TRUE(engine->Checkpoint().ok());
  engine.reset();
  ASSERT_TRUE(Engine::Open(opts, &engine).ok());
  std::string got;
  ASSERT_TRUE(engine->Get("pg1", &got).ok());
  EXPECT_EQ(got, value);
}

}  // namespace
}  // namespace test
}  // namespace ebtree
