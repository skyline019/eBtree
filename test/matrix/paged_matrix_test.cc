#include <gtest/gtest.h>

#include "matrix_test_runner.h"
#include "paged_matrix_inc.h"

namespace {

class PagedMatrixTest : public ::testing::TestWithParam<int> {};

}  // namespace

TEST_P(PagedMatrixTest, RunCase) {
  ebtree::test::RunMatrixCase(kPagedMatrixCases[GetParam()]);
}

INSTANTIATE_TEST_SUITE_P(EbMatrix, PagedMatrixTest,
                         ::testing::Range(0, kPagedMatrixCaseCount));

TEST(EbMatrixSchema, PagedCasesNonEmpty) {
  EXPECT_GE(kPagedMatrixCaseCount, 3);
}
