#include <gtest/gtest.h>

#include "matrix_test_runner.h"
#include "concurrent_matrix_inc.h"

namespace {

class ConcurrentMatrixTest : public ::testing::TestWithParam<int> {};

}  // namespace

TEST_P(ConcurrentMatrixTest, RunCase) {
  ebtree::test::RunMatrixCase(kConcurrentMatrixCases[GetParam()]);
}

INSTANTIATE_TEST_SUITE_P(EbMatrix, ConcurrentMatrixTest,
                         ::testing::Range(0, kConcurrentMatrixCaseCount));

TEST(EbMatrixSchema, ConcurrentCasesNonEmpty) {
  EXPECT_GE(kConcurrentMatrixCaseCount, 3);
}
