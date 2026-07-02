#pragma once

#include <string>
#include <vector>

struct EbMatrixCase {
  std::string id;
  std::string durability;
  std::vector<std::string> setup_ops;
  std::string run;
  std::string expect;
  std::string get_key;
  std::string get_value;
  std::string error_contains;
  std::string corrupt;
  std::string assert_stat;
  bool compress_pages{false};
  bool product_default{false};
};
