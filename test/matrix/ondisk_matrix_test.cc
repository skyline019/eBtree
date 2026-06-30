#include <gtest/gtest.h>

#include "matrix_test_runner.h"
#include "ondisk_matrix_inc.h"

namespace {

class OndiskMatrixTest : public ::testing::TestWithParam<int> {};

}  // namespace

TEST_P(OndiskMatrixTest, RunCase) {
  ebtree::test::RunMatrixCase(kOndiskMatrixCases[GetParam()]);
}

INSTANTIATE_TEST_SUITE_P(EbMatrix, OndiskMatrixTest,
                         ::testing::Range(0, kOndiskMatrixCaseCount));

TEST(EbMatrixSchema, OndiskCasesNonEmpty) {
  EXPECT_GE(kOndiskMatrixCaseCount, 3);
}
