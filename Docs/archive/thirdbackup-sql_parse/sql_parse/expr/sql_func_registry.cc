#include "sql_parse/expr/sql_func_registry.h"

#include <string_view>
#include <vector>

namespace heterodb::sql_parse {

namespace {

enum class SqlFunctionCategory {
  kString,
  kNumeric,
  kDateTime,
  kJson,
  kConditional,
  kAggregate,
};

struct SqlFunctionSpec {
  std::string_view name;
  SqlFunctionCategory category;
  bool eval_supported;
};

#define HDB_FUNC(name, cat, eval) \
  { name, SqlFunctionCategory::cat, eval }

constexpr SqlFunctionSpec kStringFunctions[] = {
    HDB_FUNC("UPPER", kString, true),
    HDB_FUNC("LOWER", kString, true),
    HDB_FUNC("UCASE", kString, true),
    HDB_FUNC("LCASE", kString, true),
    HDB_FUNC("CONCAT", kString, true),
    HDB_FUNC("CONCAT_WS", kString, true),
    HDB_FUNC("SUBSTRING", kString, true),
    HDB_FUNC("SUBSTR", kString, true),
    HDB_FUNC("MID", kString, true),
    HDB_FUNC("TRIM", kString, true),
    HDB_FUNC("LTRIM", kString, true),
    HDB_FUNC("RTRIM", kString, true),
    HDB_FUNC("LENGTH", kString, true),
    HDB_FUNC("CHAR_LENGTH", kString, true),
    HDB_FUNC("LOCATE", kString, true),
    HDB_FUNC("REPLACE", kString, true),
    HDB_FUNC("LPAD", kString, true),
    HDB_FUNC("RPAD", kString, true),
    HDB_FUNC("REVERSE", kString, true),
    HDB_FUNC("REPEAT", kString, true),
    HDB_FUNC("LEFT", kString, true),
    HDB_FUNC("RIGHT", kString, true),
    HDB_FUNC("SPACE", kString, true),
    HDB_FUNC("STRCMP", kString, true),
    HDB_FUNC("FIND_IN_SET", kString, true),
};

constexpr SqlFunctionSpec kNumericFunctions[] = {
    HDB_FUNC("ABS", kNumeric, true),
    HDB_FUNC("ROUND", kNumeric, true),
    HDB_FUNC("FLOOR", kNumeric, true),
    HDB_FUNC("CEIL", kNumeric, true),
    HDB_FUNC("CEILING", kNumeric, true),
    HDB_FUNC("MOD", kNumeric, true),
    HDB_FUNC("SIGN", kNumeric, true),
    HDB_FUNC("POW", kNumeric, true),
    HDB_FUNC("POWER", kNumeric, true),
    HDB_FUNC("GREATEST", kNumeric, true),
    HDB_FUNC("LEAST", kNumeric, true),
};

constexpr SqlFunctionSpec kDateTimeFunctions[] = {
    HDB_FUNC("DATE", kDateTime, true),
    HDB_FUNC("DATE_FORMAT", kDateTime, true),
    HDB_FUNC("DATE_ADD", kDateTime, true),
    HDB_FUNC("DATE_SUB", kDateTime, true),
    HDB_FUNC("DATEDIFF", kDateTime, true),
    HDB_FUNC("YEAR", kDateTime, true),
    HDB_FUNC("MONTH", kDateTime, true),
    HDB_FUNC("DAY", kDateTime, true),
    HDB_FUNC("NOW", kDateTime, true),
    HDB_FUNC("CURDATE", kDateTime, true),
    HDB_FUNC("CURTIME", kDateTime, true),
    HDB_FUNC("EXTRACT", kDateTime, true),
};

constexpr SqlFunctionSpec kJsonFunctions[] = {
    HDB_FUNC("JSON_EXTRACT", kJson, true),
    HDB_FUNC("JSON_UNQUOTE", kJson, true),
    HDB_FUNC("JSON_OBJECT", kJson, true),
    HDB_FUNC("JSON_ARRAY", kJson, true),
};

constexpr SqlFunctionSpec kConditionalFunctions[] = {
    HDB_FUNC("COALESCE", kConditional, true),
    HDB_FUNC("NULLIF", kConditional, true),
    HDB_FUNC("IFNULL", kConditional, true),
    HDB_FUNC("NVL", kConditional, true),
    HDB_FUNC("ISNULL", kConditional, true),
    HDB_FUNC("IF", kConditional, true),
    HDB_FUNC("CAST", kConditional, true),
    HDB_FUNC("CONVERT", kConditional, false),
};

constexpr SqlFunctionSpec kAggregateFunctions[] = {
    HDB_FUNC("COUNT", kAggregate, false),
    HDB_FUNC("SUM", kAggregate, false),
    HDB_FUNC("AVG", kAggregate, false),
    HDB_FUNC("MIN", kAggregate, false),
    HDB_FUNC("MAX", kAggregate, false),
    HDB_FUNC("GROUP_CONCAT", kAggregate, true),
};

const SqlFunctionSpec* LookupSpec(std::string_view upper_name) {
  const SqlFunctionSpec* tables[] = {
      kConditionalFunctions,
      kStringFunctions,
      kNumericFunctions,
      kDateTimeFunctions,
      kJsonFunctions,
      kAggregateFunctions,
  };
  constexpr size_t table_sizes[] = {
      sizeof(kConditionalFunctions) / sizeof(kConditionalFunctions[0]),
      sizeof(kStringFunctions) / sizeof(kStringFunctions[0]),
      sizeof(kNumericFunctions) / sizeof(kNumericFunctions[0]),
      sizeof(kDateTimeFunctions) / sizeof(kDateTimeFunctions[0]),
      sizeof(kJsonFunctions) / sizeof(kJsonFunctions[0]),
      sizeof(kAggregateFunctions) / sizeof(kAggregateFunctions[0]),
  };
  for (size_t t = 0; t < sizeof(tables) / sizeof(tables[0]); ++t) {
    for (size_t i = 0; i < table_sizes[t]; ++i) {
      if (tables[t][i].name == upper_name) {
        return &tables[t][i];
      }
    }
  }
  return nullptr;
}

}  // namespace

std::vector<std::string_view> SqlParseFunctionNames() {
  std::vector<std::string_view> out;
  const SqlFunctionSpec* tables[] = {
      kConditionalFunctions, kStringFunctions,  kNumericFunctions,
      kDateTimeFunctions,    kJsonFunctions,    kAggregateFunctions,
  };
  constexpr size_t table_sizes[] = {
      sizeof(kConditionalFunctions) / sizeof(kConditionalFunctions[0]),
      sizeof(kStringFunctions) / sizeof(kStringFunctions[0]),
      sizeof(kNumericFunctions) / sizeof(kNumericFunctions[0]),
      sizeof(kDateTimeFunctions) / sizeof(kDateTimeFunctions[0]),
      sizeof(kJsonFunctions) / sizeof(kJsonFunctions[0]),
      sizeof(kAggregateFunctions) / sizeof(kAggregateFunctions[0]),
  };
  size_t total = 0;
  for (size_t n : table_sizes) {
    total += n;
  }
  out.reserve(total);
  for (size_t t = 0; t < sizeof(tables) / sizeof(tables[0]); ++t) {
    for (size_t i = 0; i < table_sizes[t]; ++i) {
      out.push_back(tables[t][i].name);
    }
  }
  return out;
}

bool IsSqlParseSupportedFunction(std::string_view upper_name) {
  return LookupSpec(upper_name) != nullptr;
}

bool SqlFunctionEvalSupported(std::string_view upper_name) {
  const SqlFunctionSpec* spec = LookupSpec(upper_name);
  return spec != nullptr && spec->eval_supported;
}

bool SqlFunctionIsAggregate(std::string_view upper_name) {
  const SqlFunctionSpec* spec = LookupSpec(upper_name);
  return spec != nullptr && spec->category == SqlFunctionCategory::kAggregate;
}

}  // namespace heterodb::sql_parse
