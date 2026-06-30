#include <gtest/gtest.h>

#include "matrix_test_runner.h"
#include "pipeline_matrix_inc.h"

namespace {

class PipelineMatrixTest : public ::testing::TestWithParam<int> {};

}  // namespace

TEST_P(PipelineMatrixTest, RunCase) {
  ebtree::test::RunMatrixCase(kPipelineMatrixCases[GetParam()]);
}

INSTANTIATE_TEST_SUITE_P(EbMatrix, PipelineMatrixTest,
                         ::testing::Range(0, kPipelineMatrixCaseCount));

TEST(EbMatrixSchema, PipelineCasesNonEmpty) {
  EXPECT_GT(kPipelineMatrixCaseCount, 0);
}
