#include "ebtree/concept/codec/lz4_fast_codec.h"

namespace ebtree {

namespace {

constexpr uint8_t kLz4FastMagic = 0xF4;

Status PackWire(uint32_t uncompressed_size, const std::string& body,
                std::string* out) {
  if (!out) return Status::InvalidArgument("out is null");
  out->clear();
  out->reserve(4 + body.size());
  const uint32_t usize = uncompressed_size;
  out->push_back(static_cast<char>(usize & 0xFF));
  out->push_back(static_cast<char>((usize >> 8) & 0xFF));
  out->push_back(static_cast<char>((usize >> 16) & 0xFF));
  out->push_back(static_cast<char>((usize >> 24) & 0xFF));
  out->append(body);
  return Status::Ok();
}

}  // namespace

Status Lz4FastCompress(const std::string& input, Lz4FastResult* out) {
  if (!out) return Status::InvalidArgument("out is null");
  out->compressed = false;
  out->payload = input;
  out->uncompressed_size = static_cast<uint32_t>(input.size());
  if (input.size() < 16) return Status::Ok();

  size_t best_run = 1;
  size_t best_start = 0;
  for (size_t i = 0; i < input.size();) {
    size_t j = i + 1;
    while (j < input.size() && input[j] == input[i]) ++j;
    const size_t run = j - i;
    if (run > best_run) {
      best_run = run;
      best_start = i;
    }
    i = j;
  }
  if (best_run < 16) return Status::Ok();

  std::string body;
  body.reserve(6);
  body.push_back(static_cast<char>(kLz4FastMagic));
  body.push_back(input[best_start]);
  const uint32_t run_len = static_cast<uint32_t>(best_run);
  body.push_back(static_cast<char>(run_len & 0xFF));
  body.push_back(static_cast<char>((run_len >> 8) & 0xFF));
  body.push_back(static_cast<char>((run_len >> 16) & 0xFF));
  body.push_back(static_cast<char>((run_len >> 24) & 0xFF));

  std::string wire;
  const Status ps = PackWire(static_cast<uint32_t>(input.size()), body, &wire);
  if (!ps.ok()) return ps;
  if (wire.size() >= input.size()) return Status::Ok();

  out->compressed = true;
  out->payload = std::move(wire);
  out->uncompressed_size = static_cast<uint32_t>(input.size());
  return Status::Ok();
}

Status Lz4FastDecompress(const std::string& payload, uint32_t uncompressed_size,
                         std::string* out) {
  if (!out) return Status::InvalidArgument("out is null");
  if (payload.size() < 4) return Status::CorruptPage("lz4fast trunc");
  const auto* u = reinterpret_cast<const unsigned char*>(payload.data());
  const uint32_t usize = static_cast<uint32_t>(u[0]) |
                         (static_cast<uint32_t>(u[1]) << 8) |
                         (static_cast<uint32_t>(u[2]) << 16) |
                         (static_cast<uint32_t>(u[3]) << 24);
  if (usize != uncompressed_size) return Status::CorruptPage("lz4fast size mismatch");
  const std::string body = payload.substr(4);
  if (body.size() < 6 || static_cast<unsigned char>(body[0]) != kLz4FastMagic) {
    return Status::CorruptPage("lz4fast bad magic");
  }
  const char fill = body[1];
  const uint32_t run_len =
      static_cast<uint32_t>(static_cast<unsigned char>(body[2])) |
      (static_cast<uint32_t>(static_cast<unsigned char>(body[3])) << 8) |
      (static_cast<uint32_t>(static_cast<unsigned char>(body[4])) << 16) |
      (static_cast<uint32_t>(static_cast<unsigned char>(body[5])) << 24);
  if (run_len == 0 || run_len > usize) return Status::CorruptPage("lz4fast bad run");
  out->assign(static_cast<size_t>(usize), fill);
  return Status::Ok();
}

}  // namespace ebtree
