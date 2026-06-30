#include <gtest/gtest.h>

#include "ebtree_audit.h"
#include "rar_sign.h"

#if defined(EBTREE_RAR_SIGNING)
#define ED25519_NO_SEED
#include "third_party/ed25519/ed25519.h"
#endif

namespace {

TEST(RarCApi, VerifySignature) {
  const std::string body = "{\"rar_version\":\"2.0\",\"verdict\":\"PASS\"}";
#if defined(EBTREE_RAR_SIGNING)
  unsigned char seed[32] = {};
  std::string sig;
  ASSERT_TRUE(ebtree::audit::SignRarJson(
                  body, std::string(reinterpret_cast<char*>(seed), 32), &sig)
                  .ok());
  unsigned char public_key[32];
  unsigned char private_key[64];
  ed25519_create_keypair(public_key, private_key, seed);
  EXPECT_EQ(ebtree_audit_verify_signature(body.c_str(), sig.c_str(),
                                          reinterpret_cast<const char*>(public_key)),
            0);
#else
  std::string sig;
  ASSERT_TRUE(
      ebtree::audit::SignRarJson(body, "test-secret", &sig).ok());
  EXPECT_EQ(ebtree_audit_verify_signature(body.c_str(), sig.c_str(), "test-secret"),
            0);
#endif
}

}  // namespace
