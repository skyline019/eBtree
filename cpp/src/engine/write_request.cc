#include "ebtree/engine/write_request.h"

namespace ebtree {

thread_local WriteRequest g_write_req;

WriteRequest* CurrentWriteRequest() { return &g_write_req; }

}  // namespace ebtree
