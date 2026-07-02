#pragma once

#include <string>

namespace ebtree {

struct WriteRequest {
  std::string key;
  std::string value;
  bool is_delete{false};
  uint32_t txn_id{0};
};

extern thread_local WriteRequest g_write_req;

WriteRequest* CurrentWriteRequest();

}  // namespace ebtree
