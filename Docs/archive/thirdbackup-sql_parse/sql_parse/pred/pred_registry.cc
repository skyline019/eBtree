#include "sql_parse/pred/pred_registry.h"

namespace heterodb::sql_parse {

FirstMatchRegistry& PredRegistry::rules(PredTrack track) {
  return track == PredTrack::kColumn ? column_rules_ : expr_rules_;
}

const FirstMatchRegistry& PredRegistry::rules(PredTrack track) const {
  return track == PredTrack::kColumn ? column_rules_ : expr_rules_;
}

}  // namespace heterodb::sql_parse
