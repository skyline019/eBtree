#include "agg_engine.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>

#include "sql/eval/sql_value.h"
#include "sql/eval/type_affinity.h"

namespace ebtree {
namespace sql {
namespace {

std::string FieldVal(const RowMap& r, const std::string& col) {
  if (col == "*") return "1";
  if (r.count(col)) return r.at(col);
  const auto dot = col.find('.');
  if (dot != std::string::npos) {
    const std::string bare = col.substr(dot + 1);
    if (r.count(bare)) return r.at(bare);
  }
  if (r.count("value") && (col == "value" || col.empty())) return r.at("value");
  if (r.count("key") && (col == "key" || col.empty())) return r.at("key");
  return "";
}

double ToNum(const std::string& v) {
  if (v.empty()) return 0.0;
  char* end = nullptr;
  const double d = std::strtod(v.c_str(), &end);
  if (end != v.c_str() && *end == '\0') return d;
  return 0.0;
}

bool IsNumericText(const std::string& v) {
  if (v.empty()) return false;
  char* end = nullptr;
  std::strtod(v.c_str(), &end);
  return end != v.c_str() && *end == '\0';
}

double NumericOrZero(const std::string& v) {
  if (v.empty()) return 0.0;
  if (IsNumericText(v)) return ToNum(v);
  return 0.0;
}

std::string DistinctNumericKey(const std::string& v) {
  const double n = NumericOrZero(v);
  char buf[64];
  std::snprintf(buf, sizeof(buf), "%.17g", n);
  return std::string(buf);
}

}  // namespace

Status AggEngine::Validate(const AggregateSpec& agg) {
  if (agg.func == "COUNT" && agg.distinct && agg.column == "*") {
    return Status::InvalidArgument("COUNT(DISTINCT *) not supported");
  }
  if (agg.func == "GROUP_CONCAT" && agg.distinct && agg.separator != ",") {
    return Status::InvalidArgument("DISTINCT GROUP_CONCAT with separator");
  }
  return Status::Ok();
}

void AggEngine::Accumulate(AggBucket* b, const AggregateSpec& agg,
                           const RowMap& r) {
  const std::string v = agg.column == "*" ? "1" : FieldVal(r, agg.column);
  if (agg.func == "COUNT") {
    if (agg.distinct && agg.column != "*") {
      if (!v.empty()) b->distinct_vals.insert(v);
    } else if (agg.column != "*") {
      if (!v.empty()) b->non_null_count++;
    }
    return;
  }
  if (agg.func == "GROUP_CONCAT") {
    if (v.empty()) return;
    if (agg.distinct) {
      if (!b->distinct_vals.insert(v).second) return;
    }
    b->concat_parts.push_back(v);
    return;
  }
  if (agg.func == "SUM" || agg.func == "TOTAL") {
    const double nv = NumericOrZero(v);
    if (agg.distinct) {
      if (!b->distinct_vals.insert(DistinctNumericKey(v)).second) return;
    }
    b->sum += nv;
    return;
  }
  if (agg.func == "AVG") {
    if (v.empty()) return;
    if (agg.distinct) {
      if (!b->distinct_vals.insert(DistinctNumericKey(v)).second) return;
    }
    b->avg_sum += NumericOrZero(v);
    b->avg_count++;
    return;
  }
  if (agg.func == "MIN" || agg.func == "MAX") {
    if (v.empty()) return;
    if (agg.distinct) {
      if (!b->distinct_vals.insert(DistinctNumericKey(v)).second) return;
    }
    const double num = NumericOrZero(v);
    if (agg.func == "MIN") {
      if (!b->has_min || num < b->min_num) {
        b->min_num = num;
        b->min_is_text = false;
        b->has_min = true;
      }
    } else {
      if (!b->has_max || num > b->max_num) {
        b->max_num = num;
        b->max_is_text = false;
        b->has_max = true;
      }
    }
  }
}

std::string AggEngine::Finalize(const AggregateSpec& agg, const AggBucket& b,
                              size_t row_count, char coltype) {
  if (agg.func == "COUNT") {
    if (agg.distinct && agg.column != "*") {
      return std::to_string(b.distinct_vals.size());
    }
    if (agg.column == "*") {
      return std::to_string(row_count);
    }
    return std::to_string(b.non_null_count);
  }
  if (agg.func == "SUM" || agg.func == "TOTAL") {
    const char ct = coltype ? coltype : 'I';
    if (ct == 'R') {
      return SqlValue::Real(b.sum).ToDisplayString('R');
    }
    if (b.sum == std::floor(b.sum)) {
      return std::to_string(static_cast<int64_t>(b.sum));
    }
    return SqlValue::Real(b.sum).ToDisplayString('R');
  }
  if (agg.func == "AVG") {
    if (b.avg_count == 0) return "";
    const double avg = b.avg_sum / static_cast<double>(b.avg_count);
    return SqlValue::Real(avg).ToDisplayString(coltype ? coltype : 'R');
  }
  if (agg.func == "MIN" && b.has_min) {
    if (b.min_is_text) return b.min_text;
    if (b.min_num == std::floor(b.min_num)) {
      return std::to_string(static_cast<int64_t>(b.min_num));
    }
    return SqlValue::Real(b.min_num).ToDisplayString(coltype ? coltype : 'R');
  }
  if (agg.func == "MAX" && b.has_max) {
    if (b.max_is_text) return b.max_text;
    if (b.max_num == std::floor(b.max_num)) {
      return std::to_string(static_cast<int64_t>(b.max_num));
    }
    return SqlValue::Real(b.max_num).ToDisplayString(coltype ? coltype : 'R');
  }
  if (agg.func == "GROUP_CONCAT") {
    std::string out;
    for (size_t i = 0; i < b.concat_parts.size(); ++i) {
      if (i > 0) out += agg.separator;
      out += b.concat_parts[i];
    }
    return out;
  }
  return "";
}

}  // namespace sql
}  // namespace ebtree
