#include <gtest/gtest.h>

#include "matrix_test_runner.h"
#include "no_fallback_matrix_inc.h"

namespace {

class NoFallbackMatrixTest : public ::testing::TestWithParam<int> {};

}  // namespace

TEST_P(NoFallbackMatrixTest, RunCase) {
  ebtree::test::RunMatrixCase(kNoFallbackMatrixCases[GetParam()]);
}

INSTANTIATE_TEST_SUITE_P(EbMatrix, NoFallbackMatrixTest,
                         ::testing::Range(0, kNoFallbackMatrixCaseCount));

TEST(EbMatrixSchema, NoFallbackCasesNonEmpty) {
  EXPECT_GT(kNoFallbackMatrixCaseCount, 0);
}
