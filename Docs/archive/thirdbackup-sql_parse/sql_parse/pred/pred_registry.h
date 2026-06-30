#pragma once

#include "sql_parse/core/parse_registry.h"
#include "sql_parse/pred/pred_track.h"

namespace heterodb::sql_parse {

class PredRegistry {
 public:
  FirstMatchRegistry& rules(PredTrack track);
  const FirstMatchRegistry& rules(PredTrack track) const;

 private:
  FirstMatchRegistry column_rules_;
  FirstMatchRegistry expr_rules_;
};

}  // namespace heterodb::sql_parse
