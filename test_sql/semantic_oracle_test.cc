#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "sqllogic_common.h"

namespace ebtree {
namespace test {
namespace {

using sqllogic::FailKind;
using sqllogic::LoadSqllogicFile;
using sqllogic::RunCase;
using sqllogic::SqllogicCase;

TEST(SemanticOracle, SemanticCorpusStrict100) {
  const auto cases =
      LoadSqllogicFile("test/data/sqllogic/semantic/semantic.test");
  ASSERT_GE(cases.size(), 20u) << "semantic corpus too small";
  int passed = 0;
  std::vector<std::string> failures;
  for (const auto& c : cases) {
    const auto result = RunCase(c);
    if (result.kind == FailKind::kPass) {
      ++passed;
      continue;
    }
    failures.push_back(c.name + ": " + result.detail);
  }
  for (const auto& f : failures) {
    ADD_FAILURE() << f;
  }
  EXPECT_EQ(passed, static_cast<int>(cases.size()))
      << "semantic oracle pass rate must be 100%";
}

}  // namespace
}  // namespace test
}  // namespace ebtree
