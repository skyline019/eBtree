#include <gtest/gtest.h>

#include "matrix_test_runner.h"
#include "chaos_matrix_inc.h"

namespace {

class ChaosMatrixTest : public ::testing::TestWithParam<int> {};

}  // namespace

TEST_P(ChaosMatrixTest, RunCase) {
  ebtree::test::RunMatrixCase(kChaosMatrixCases[GetParam()]);
}

INSTANTIATE_TEST_SUITE_P(EbMatrix, ChaosMatrixTest,
                         ::testing::Range(0, kChaosMatrixCaseCount));

TEST(EbMatrixSchema, ChaosCasesNonEmpty) {
  EXPECT_GE(kChaosMatrixCaseCount, 3);
}
