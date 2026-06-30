#include <gtest/gtest.h>

#include "durability_matrix_inc.h"
#include "matrix_test_runner.h"

namespace {

class DurabilityMatrixTest
    : public ::testing::TestWithParam<int> {};

}  // namespace

TEST_P(DurabilityMatrixTest, RunCase) {
  ebtree::test::RunMatrixCase(kDurabilityMatrixCases[GetParam()]);
}

INSTANTIATE_TEST_SUITE_P(EbMatrix, DurabilityMatrixTest,
                         ::testing::Range(0, kDurabilityMatrixCaseCount));

TEST(EbMatrixSchema, DurabilityCasesNonEmpty) {
  EXPECT_GT(kDurabilityMatrixCaseCount, 0);
  for (int i = 0; i < kDurabilityMatrixCaseCount; ++i) {
    EXPECT_FALSE(kDurabilityMatrixCases[i].id.empty());
  }
}
