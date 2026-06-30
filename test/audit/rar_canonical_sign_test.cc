#include <gtest/gtest.h>

#include "rar_sign.h"
#include "engine_test_util.h"

#if defined(EBTREE_RAR_SIGNING)
#define ED25519_NO_SEED
#include "third_party/ed25519/ed25519.h"
#endif

namespace ebtree {
namespace audit {
namespace {

TEST(RarCanonical, StripSignatureStable) {
  const std::string body =
      "{\n  \"rar_version\": \"2.0\",\n  \"verdict\": \"PASS\",\n  "
      "\"signature\": \"abc\"\n}";
  const std::string canon = CanonicalizeRarForSigning(body);
  EXPECT_EQ(canon.find("signature"), std::string::npos);
  EXPECT_NE(canon.find("rar_version"), std::string::npos);
}

#if defined(EBTREE_RAR_SIGNING)
TEST(RarCanonical, DeterministicSign) {
  const std::string body = "{\"rar_version\":\"2.0\",\"verdict\":\"PASS\"}";
  unsigned char seed[32] = {};
  std::string sig1;
  std::string sig2;
  ASSERT_TRUE(SignRarJson(body, std::string(reinterpret_cast<char*>(seed), 32), &sig1).ok());
  ASSERT_TRUE(SignRarJson(body, std::string(reinterpret_cast<char*>(seed), 32), &sig2).ok());
  EXPECT_EQ(sig1, sig2);
  unsigned char public_key[32];
  unsigned char private_key[64];
  ed25519_create_keypair(public_key, private_key, seed);
  const std::string pubkey(reinterpret_cast<char*>(public_key), 32);
  EXPECT_TRUE(VerifyRarSignature(body, sig1, pubkey).ok());
}
#endif

}  // namespace
}  // namespace audit
}  // namespace ebtree
