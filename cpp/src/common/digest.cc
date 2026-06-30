#include "ebtree/common/digest.h"

#include <cstdio>

namespace ebtree {

namespace {

struct Sha256Ctx {
  uint32_t state[8];
  uint64_t bitlen;
  uint8_t data[64];
  size_t datalen;
};

constexpr uint32_t kSha256K[64] = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1,
    0x923f82a4, 0xab1c5ed5, 0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3,
    0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174, 0xe49b69c1, 0xefbe4786,
    0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147,
    0x06ca6351, 0x14292967, 0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13,
    0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85, 0xa2bfe8a1, 0xa81a664b,
    0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a,
    0x5b9cca4f, 0x682e6ff3, 0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208,
    0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2};

inline uint32_t RoR(uint32_t x, uint32_t n) { return (x >> n) | (x << (32 - n)); }

inline uint32_t Ch(uint32_t x, uint32_t y, uint32_t z) {
  return (x & y) ^ (~x & z);
}

inline uint32_t Maj(uint32_t x, uint32_t y, uint32_t z) {
  return (x & y) ^ (x & z) ^ (y & z);
}

inline uint32_t Sig0(uint32_t x) {
  return RoR(x, 2) ^ RoR(x, 13) ^ RoR(x, 22);
}

inline uint32_t Sig1(uint32_t x) {
  return RoR(x, 6) ^ RoR(x, 11) ^ RoR(x, 25);
}

inline uint32_t sig0(uint32_t x) {
  return RoR(x, 7) ^ RoR(x, 18) ^ (x >> 3);
}

inline uint32_t sig1(uint32_t x) {
  return RoR(x, 17) ^ RoR(x, 19) ^ (x >> 10);
}

void Sha256Transform(Sha256Ctx* ctx, const uint8_t data[64]) {
  uint32_t m[64];
  for (int i = 0; i < 16; ++i) {
    m[i] = (static_cast<uint32_t>(data[i * 4]) << 24) |
           (static_cast<uint32_t>(data[i * 4 + 1]) << 16) |
           (static_cast<uint32_t>(data[i * 4 + 2]) << 8) |
           static_cast<uint32_t>(data[i * 4 + 3]);
  }
  for (int i = 16; i < 64; ++i) {
    m[i] = sig1(m[i - 2]) + m[i - 7] + sig0(m[i - 15]) + m[i - 16];
  }

  uint32_t a = ctx->state[0];
  uint32_t b = ctx->state[1];
  uint32_t c = ctx->state[2];
  uint32_t d = ctx->state[3];
  uint32_t e = ctx->state[4];
  uint32_t f = ctx->state[5];
  uint32_t g = ctx->state[6];
  uint32_t h = ctx->state[7];

  for (int i = 0; i < 64; ++i) {
    const uint32_t t1 = h + Sig1(e) + Ch(e, f, g) + kSha256K[i] + m[i];
    const uint32_t t2 = Sig0(a) + Maj(a, b, c);
    h = g;
    g = f;
    f = e;
    e = d + t1;
    d = c;
    c = b;
    b = a;
    a = t1 + t2;
  }

  ctx->state[0] += a;
  ctx->state[1] += b;
  ctx->state[2] += c;
  ctx->state[3] += d;
  ctx->state[4] += e;
  ctx->state[5] += f;
  ctx->state[6] += g;
  ctx->state[7] += h;
}

void Sha256Init(Sha256Ctx* ctx) {
  ctx->datalen = 0;
  ctx->bitlen = 0;
  ctx->state[0] = 0x6a09e667;
  ctx->state[1] = 0xbb67ae85;
  ctx->state[2] = 0x3c6ef372;
  ctx->state[3] = 0xa54ff53a;
  ctx->state[4] = 0x510e527f;
  ctx->state[5] = 0x9b05688c;
  ctx->state[6] = 0x1f83d9ab;
  ctx->state[7] = 0x5be0cd19;
}

void Sha256Update(Sha256Ctx* ctx, const uint8_t* data, size_t len) {
  for (size_t i = 0; i < len; ++i) {
    ctx->data[ctx->datalen++] = data[i];
    if (ctx->datalen == 64) {
      Sha256Transform(ctx, ctx->data);
      ctx->bitlen += 512;
      ctx->datalen = 0;
    }
  }
}

void Sha256Final(Sha256Ctx* ctx, uint8_t hash[32]) {
  const size_t i = ctx->datalen;
  ctx->data[i] = 0x80;
  if (i < 56) {
    for (size_t j = i + 1; j < 56; ++j) ctx->data[j] = 0;
  } else {
    for (size_t j = i + 1; j < 64; ++j) ctx->data[j] = 0;
    Sha256Transform(ctx, ctx->data);
    for (size_t j = 0; j < 56; ++j) ctx->data[j] = 0;
  }

  ctx->bitlen += ctx->datalen * 8;
  ctx->data[63] = static_cast<uint8_t>(ctx->bitlen);
  ctx->data[62] = static_cast<uint8_t>(ctx->bitlen >> 8);
  ctx->data[61] = static_cast<uint8_t>(ctx->bitlen >> 16);
  ctx->data[60] = static_cast<uint8_t>(ctx->bitlen >> 24);
  ctx->data[59] = static_cast<uint8_t>(ctx->bitlen >> 32);
  ctx->data[58] = static_cast<uint8_t>(ctx->bitlen >> 40);
  ctx->data[57] = static_cast<uint8_t>(ctx->bitlen >> 48);
  ctx->data[56] = static_cast<uint8_t>(ctx->bitlen >> 56);
  Sha256Transform(ctx, ctx->data);

  for (int j = 0; j < 4; ++j) {
    for (int k = 0; k < 8; ++k) {
      hash[k * 4 + j] = static_cast<uint8_t>(ctx->state[k] >> (24 - j * 8));
    }
  }
}

std::string BytesToHex(const uint8_t* data, size_t len) {
  static const char* kHex = "0123456789abcdef";
  std::string out;
  out.reserve(len * 2);
  for (size_t i = 0; i < len; ++i) {
    out.push_back(kHex[(data[i] >> 4) & 0xF]);
    out.push_back(kHex[data[i] & 0xF]);
  }
  return out;
}

}  // namespace

std::string Sha256Hex(const uint8_t* data, size_t len) {
  Sha256Ctx ctx;
  Sha256Init(&ctx);
  Sha256Update(&ctx, data, len);
  uint8_t hash[32];
  Sha256Final(&ctx, hash);
  return BytesToHex(hash, 32);
}

std::string Sha256HexString(const std::string& data) {
  return Sha256Hex(reinterpret_cast<const uint8_t*>(data.data()), data.size());
}

}  // namespace ebtree
