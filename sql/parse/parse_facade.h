#pragma once

// SQL parse pipeline (RAR-style facade):
//   RegistryParser  ->  NativeParser  ->  LexPipeline + StmtClassifier
//                       ->  FirstMatchRegistry (DML/DDL rules)
//                       ->  ExprParse / SelectParse / AdvancedParse
//
// Public entry: ebtree::sql::parse::RegistryParser

#include "sql/parse/registry/registry_parser.h"

namespace ebtree {
namespace sql {
namespace parse {

using SqlParser = RegistryParser;

}  // namespace parse
}  // namespace sql
}  // namespace ebtree
