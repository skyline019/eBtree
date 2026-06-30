#pragma once

#include <string>

namespace ebtree {

struct WriteRequest {
  std::string key;
  std::string value;
  bool is_delete{false};
};

extern thread_local WriteRequest g_write_req;

WriteRequest* CurrentWriteRequest();

}  // namespace ebtree
