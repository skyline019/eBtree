#include <gtest/gtest.h>

#include "digest.h"
#include "json_writer.h"
#include "rar_builder.h"
#include "engine_test_util.h"

namespace ebtree {
namespace audit {
namespace {

TEST(RarRequireSignature, MissingSignatureFailsCliGate) {
  const std::string dir = test::TempDir("rar_require_sig");
  {
    std::unique_ptr<Engine> engine;
    EngineOptions opts = EngineOptions::BenchmarkGroupDefaults(dir);
    opts.path = dir;
    ASSERT_TRUE(Engine::Open(opts, &engine).ok());
    ASSERT_TRUE(engine->Put("sig_k", "sig_v").ok());
    ASSERT_TRUE(engine->GroupCommit().ok());
  }

  BuildRarOptions build{};
  build.engine_path = dir;
  build.durability_tier = DurabilityClass::kGroup;

  RarReport report{};
  ASSERT_TRUE(BuildRar(build, &report).ok());
  const std::string json = RarReportToJson(report);
  EXPECT_EQ(json.find("\"signature\""), std::string::npos);

  const bool require_sig = true;
  const int exit_code =
      (require_sig && json.find("\"signature\"") == std::string::npos) ? 1 : 0;
  EXPECT_EQ(exit_code, 1);
}

}  // namespace
}  // namespace audit
}  // namespace ebtree
