#include "demo_flows.h"

#include <cstdio>
#include <filesystem>

int main(int argc, char** argv) {
  std::string dir = "demo_data_industrial";
  if (argc > 1) dir = argv[1];
  std::filesystem::create_directories(dir);
  return ebtree::demo::RunIndustrialFlow(dir);
}
