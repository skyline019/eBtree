#include <gtest/gtest.h>

#include <filesystem>
#include <memory>
#include <string>

#include "engine_test_util.h"
#include "ebtree/engine/engine.h"

namespace ebtree {
namespace test {
namespace {

std::uintmax_t DataFileSize(const std::string& dir) {
  const auto path = (std::filesystem::path(dir) / "shard0.data").string();
  if (!std::filesystem::exists(path)) return 0;
  return std::filesystem::file_size(path);
}

TEST(EbPipelineCompressPerf, ProductionCompressDefaultsRoundtrip) {
  const std::string dir = ebtree::test::TempDir("compress_prod_defaults");
  const std::string payload(512, 'z');
  EngineOptions opts = EngineOptions::ProductionCompressDefaults(dir);
  std::unique_ptr<Engine> engine;
  ASSERT_TRUE(Engine::Open(opts, &engine).ok());
  ASSERT_TRUE(engine->Put("ck", payload).ok());
  ASSERT_TRUE(engine->Checkpoint().ok());
  std::string value;
  ASSERT_TRUE(engine->Get("ck", &value).ok());
  EXPECT_EQ(value, payload);
  EXPECT_GT(engine->stats().compress_bytes_in, 0u);
}

TEST(EbPipelineCompressPerf, CompressValuesReducesDataFileSize) {
  const std::string raw_dir = TempDir("compress_raw");
  const std::string cmp_dir = TempDir("compress_on");
  const std::string payload(256, 'x');
  std::string json_like = "{\"key\":\"value\",\"data\":\"";
  json_like += payload;
  json_like += "\"}";

  {
    EngineOptions opts{};
    opts.path = raw_dir;
    opts.compress_values = false;
    std::unique_ptr<Engine> engine;
    ASSERT_TRUE(Engine::Open(opts, &engine).ok());
    for (int i = 0; i < 500; ++i) {
      ASSERT_TRUE(engine->Put("k" + std::to_string(i), json_like).ok());
    }
    ASSERT_TRUE(engine->Checkpoint().ok());
  }
  {
    EngineOptions opts{};
    opts.path = cmp_dir;
    opts.compress_values = true;
    std::unique_ptr<Engine> engine;
    ASSERT_TRUE(Engine::Open(opts, &engine).ok());
    for (int i = 0; i < 500; ++i) {
      ASSERT_TRUE(engine->Put("k" + std::to_string(i), json_like).ok());
    }
    ASSERT_TRUE(engine->Checkpoint().ok());
  }

  const auto raw_size = DataFileSize(raw_dir);
  const auto cmp_size = DataFileSize(cmp_dir);
  ASSERT_GT(raw_size, 0u);
  ASSERT_GT(cmp_size, 0u);
  EXPECT_LT(cmp_size, raw_size * 7 / 10)
      << "raw=" << raw_size << " compressed=" << cmp_size;
}

}  // namespace
}  // namespace test
}  // namespace ebtree
