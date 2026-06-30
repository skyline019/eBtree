#include <gtest/gtest.h>

#include "matrix_test_runner.h"
#include "recovery_matrix_inc.h"

namespace {

class RecoveryMatrixTest : public ::testing::TestWithParam<int> {};

}  // namespace

TEST_P(RecoveryMatrixTest, RunCase) {
  ebtree::test::RunMatrixCase(kRecoveryMatrixCases[GetParam()]);
}

INSTANTIATE_TEST_SUITE_P(EbMatrix, RecoveryMatrixTest,
                         ::testing::Range(0, kRecoveryMatrixCaseCount));

TEST(EbMatrixSchema, RecoveryCasesNonEmpty) {
  EXPECT_GT(kRecoveryMatrixCaseCount, 0);
}
