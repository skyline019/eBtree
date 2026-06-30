#pragma once

#include <string>

#include "ebtree/common/status.h"

namespace ebtree {
namespace audit {

Status SignRarJson(const std::string& json_body, const std::string& secret_key,
                   std::string* signature_out);

Status VerifyRarSignature(const std::string& json_body,
                          const std::string& signature,
                          const std::string& secret_key);

std::string StripSignatureField(const std::string& json);
std::string CanonicalizeRarForSigning(const std::string& json);

}  // namespace audit
}  // namespace ebtree
