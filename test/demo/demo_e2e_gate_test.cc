#include <gtest/gtest.h>

#include "demo_flows.h"
#include "engine_test_util.h"

using namespace ebtree;
using namespace ebtree::demo;

TEST(DemoE2eGate, IndustrialFlow) {
  const std::string dir = test::TempDir("demo_e2e_industrial");
  EXPECT_EQ(RunIndustrialFlow(dir), 0);
}

TEST(DemoE2eGate, MedicalFlow) {
  const std::string dir = test::TempDir("demo_e2e_medical");
  EXPECT_EQ(RunMedicalFlow(dir), 0);
}

TEST(DemoE2eGate, FinanceFlow) {
  const std::string dir = test::TempDir("demo_e2e_finance");
  EXPECT_EQ(RunFinanceFlow(dir), 0);
}
