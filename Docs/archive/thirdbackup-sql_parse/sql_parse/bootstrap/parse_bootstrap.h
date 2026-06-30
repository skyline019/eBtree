#pragma once

#include <mutex>
#include <string>

#include "common/status.h"
#include "concept/query/ast.h"

namespace heterodb::sql_parse {

class ParseBootstrap {
 public:
  static ParseBootstrap& Global();

  void InstallAll();
  Status Parse(const std::string& sql, SqlStatement* out,
               const std::string& current_database = "default");

 private:
  ParseBootstrap();
  ~ParseBootstrap();
  bool installed_{false};
  std::once_flag install_once_;
  class Impl;
  Impl* impl_;
};

}  // namespace heterodb::sql_parse
