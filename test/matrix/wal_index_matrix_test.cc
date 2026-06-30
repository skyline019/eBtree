#include <gtest/gtest.h>

#include "matrix_test_runner.h"
#include "wal_index_matrix_inc.h"

namespace {

class WalIndexMatrixTest : public ::testing::TestWithParam<int> {};

}  // namespace

TEST_P(WalIndexMatrixTest, RunCase) {
  ebtree::test::RunMatrixCase(kWalIndexMatrixCases[GetParam()]);
}

INSTANTIATE_TEST_SUITE_P(EbMatrix, WalIndexMatrixTest,
                         ::testing::Range(0, kWalIndexMatrixCaseCount));

TEST(EbMatrixSchema, WalIndexCasesNonEmpty) {
  EXPECT_GE(kWalIndexMatrixCaseCount, 1);
}
