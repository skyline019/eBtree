#include "rar_sign.h"

#include "digest.h"

#if defined(EBTREE_RAR_SIGNING)
#define ED25519_NO_SEED
#include "third_party/ed25519/ed25519.h"
#endif

#include <cctype>
#include <vector>

namespace ebtree {
namespace audit {

namespace {

bool IsHex(const std::string& s) {
  for (char c : s) {
    if (!std::isxdigit(static_cast<unsigned char>(c))) return false;
  }
  return !s.empty();
}

std::vector<unsigned char> HexDecode(const std::string& hex) {
  std::vector<unsigned char> out;
  if (hex.size() % 2 != 0) return out;
  for (size_t i = 0; i < hex.size(); i += 2) {
    const unsigned v =
        (std::isdigit(hex[i]) ? hex[i] - '0'
                              : std::tolower(hex[i]) - 'a' + 10) << 4;
    const unsigned v2 =
        (std::isdigit(hex[i + 1]) ? hex[i + 1] - '0'
                                  : std::tolower(hex[i + 1]) - 'a' + 10);
    out.push_back(static_cast<unsigned char>(v | v2));
  }
  return out;
}

std::string Base64Encode(const unsigned char* data, size_t len) {
  static const char* kAlphabet =
      "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  std::string out;
  for (size_t i = 0; i < len; i += 3) {
    const uint32_t n =
        (static_cast<uint32_t>(data[i]) << 16) |
        ((i + 1 < len) ? static_cast<uint32_t>(data[i + 1]) << 8 : 0) |
        ((i + 2 < len) ? static_cast<uint32_t>(data[i + 2]) : 0);
    out.push_back(kAlphabet[(n >> 18) & 0x3F]);
    out.push_back(kAlphabet[(n >> 12) & 0x3F]);
    out.push_back(i + 1 < len ? kAlphabet[(n >> 6) & 0x3F] : '=');
    out.push_back(i + 2 < len ? kAlphabet[n & 0x3F] : '=');
  }
  return out;
}

std::vector<unsigned char> Base64Decode(const std::string& in) {
  auto val = [](char c) -> int {
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '+') return 62;
    if (c == '/') return 63;
    return -1;
  };
  std::vector<unsigned char> out;
  int buf = 0;
  int bits = 0;
  for (char c : in) {
    if (c == '=' || std::isspace(static_cast<unsigned char>(c))) continue;
    const int v = val(c);
    if (v < 0) return {};
    buf = (buf << 6) | v;
    bits += 6;
    if (bits >= 8) {
      bits -= 8;
      out.push_back(static_cast<unsigned char>((buf >> bits) & 0xFF));
    }
  }
  return out;
}

std::vector<unsigned char> ParseKeyMaterial(const std::string& key, size_t size) {
  if (key.size() == size) {
    std::vector<unsigned char> out(size);
    for (size_t i = 0; i < size; ++i) out[i] = static_cast<unsigned char>(key[i]);
    return out;
  }
  if (IsHex(key) && key.size() == size * 2) return HexDecode(key);
  return {};
}

#if defined(EBTREE_RAR_SIGNING)
Status SignEd25519(const std::string& canonical, const std::string& seed_key,
                   std::string* signature_out) {
  const std::vector<unsigned char> seed = ParseKeyMaterial(seed_key, 32);
  if (seed.size() != 32) {
    return Status::InvalidArgument("Ed25519 signing requires 32-byte seed key");
  }
  unsigned char public_key[32];
  unsigned char private_key[64];
  ed25519_create_keypair(public_key, private_key, seed.data());

  unsigned char sig[64];
  ed25519_sign(sig, reinterpret_cast<const unsigned char*>(canonical.data()),
                 canonical.size(), public_key, private_key);
  *signature_out = Base64Encode(sig, 64);
  return Status::Ok();
}

Status VerifyEd25519(const std::string& canonical, const std::string& signature,
                     const std::string& pubkey_key) {
  const std::vector<unsigned char> pubkey = ParseKeyMaterial(pubkey_key, 32);
  if (pubkey.size() != 32) {
    return Status::InvalidArgument("Ed25519 verify requires 32-byte public key");
  }
  const std::vector<unsigned char> sig = Base64Decode(signature);
  if (sig.size() != 64) {
    return Status::Corrupt("invalid Ed25519 signature encoding");
  }
  if (!ed25519_verify(sig.data(),
                      reinterpret_cast<const unsigned char*>(canonical.data()),
                      canonical.size(), pubkey.data())) {
    return Status::Corrupt("rar signature mismatch");
  }
  return Status::Ok();
}
#endif

}  // namespace

std::string StripSignatureField(const std::string& json) {
  const auto pos = json.find("\"signature\"");
  if (pos == std::string::npos) return json;
  const auto line_start = json.rfind('\n', pos);
  const auto next = json.find('\n', pos);
  if (line_start == std::string::npos || next == std::string::npos) return json;
  std::string out = json.substr(0, line_start);
  if (!out.empty() && out.back() == ',') out.pop_back();
  out += json.substr(next);
  return out;
}

std::string CanonicalizeRarForSigning(const std::string& json) {
  return StripSignatureField(json);
}

Status SignRarJson(const std::string& json_body, const std::string& secret_key,
                   std::string* signature_out) {
  if (!signature_out) return Status::InvalidArgument("signature_out is null");
  const std::string canonical = CanonicalizeRarForSigning(json_body);
#if defined(EBTREE_RAR_SIGNING)
  return SignEd25519(canonical, secret_key, signature_out);
#else
  *signature_out = "ed25519-host-v1:" + Sha256HexString(canonical + secret_key);
  return Status::Ok();
#endif
}

Status VerifyRarSignature(const std::string& json_body,
                            const std::string& signature,
                            const std::string& secret_key) {
  const std::string canonical = CanonicalizeRarForSigning(json_body);
#if defined(EBTREE_RAR_SIGNING)
  if (signature.rfind("ed25519-host-v1:", 0) == 0) {
    return Status::Corrupt("stub signature rejected when signing enabled");
  }
  return VerifyEd25519(canonical, signature, secret_key);
#else
  std::string expected;
  const Status st = SignRarJson(json_body, secret_key, &expected);
  if (!st.ok()) return st;
  if (expected != signature) {
    return Status::Corrupt("rar signature mismatch");
  }
  return Status::Ok();
#endif
}

}  // namespace audit
}  // namespace ebtree
