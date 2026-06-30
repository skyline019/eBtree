#pragma once

#include <string>
#include <unordered_set>
#include <vector>

#include "sql/ast/expr_ast.h"
#include "sql/eval/expr_eval.h"
#include "ebtree/common/status.h"

namespace ebtree {
namespace sql {

struct AggBucket {
  int64_t count{0};
  int64_t non_null_count{0};
  double sum{0.0};
  double avg_sum{0.0};
  int64_t avg_count{0};
  std::string min_text;
  std::string max_text;
  double min_num{0.0};
  double max_num{0.0};
  bool has_min{false};
  bool has_max{false};
  bool min_is_text{false};
  bool max_is_text{false};
  std::unordered_set<std::string> distinct_vals;
  std::vector<std::string> concat_parts;
};

class AggEngine {
 public:
  static Status Validate(const AggregateSpec& agg);
  static void Accumulate(AggBucket* bucket, const AggregateSpec& agg,
                         const RowMap& row);
  static std::string Finalize(const AggregateSpec& agg, const AggBucket& bucket,
                              size_t row_count, char coltype = 0);
};

}  // namespace sql
}  // namespace ebtree
