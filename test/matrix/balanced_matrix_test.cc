#include <gtest/gtest.h>

#include "balanced_matrix_inc.h"
#include "matrix_test_runner.h"

namespace {

class BalancedMatrixTest : public ::testing::TestWithParam<int> {};

}  // namespace

TEST_P(BalancedMatrixTest, RunCase) {
  ebtree::test::RunMatrixCase(kBalancedMatrixCases[GetParam()]);
}

INSTANTIATE_TEST_SUITE_P(EbMatrix, BalancedMatrixTest,
                         ::testing::Range(0, kBalancedMatrixCaseCount));

TEST(EbMatrixSchema, BalancedCasesNonEmpty) {
  EXPECT_GT(kBalancedMatrixCaseCount, 0);
}
