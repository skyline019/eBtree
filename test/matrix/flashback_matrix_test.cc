#include <gtest/gtest.h>

#include "matrix_test_runner.h"
#include "flashback_matrix_inc.h"

namespace {

class FlashbackMatrixTest : public ::testing::TestWithParam<int> {};

}  // namespace

TEST_P(FlashbackMatrixTest, RunCase) {
  ebtree::test::RunMatrixCase(kFlashbackMatrixCases[GetParam()]);
}

INSTANTIATE_TEST_SUITE_P(EbMatrix, FlashbackMatrixTest,
                         ::testing::Range(0, kFlashbackMatrixCaseCount));

TEST(EbMatrixSchema, FlashbackCasesNonEmpty) {
  EXPECT_GE(kFlashbackMatrixCaseCount, 6);
}
