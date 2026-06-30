#include "row_codec.h"

#include <cstring>

namespace ebtree {
namespace sql {

namespace {

constexpr char kBinaryMagic[] = {'E', 'B', 'R', 3};

std::string JsonEscape(const std::string& s) {
  std::string out;
  out.reserve(s.size() + 4);
  for (char c : s) {
    if (c == '"') out += "\\\"";
    else if (c == '\\') out += "\\\\";
    else out.push_back(c);
  }
  return out;
}

void AppendU32(std::string* out, uint32_t v) {
  out->push_back(static_cast<char>(v & 0xFF));
  out->push_back(static_cast<char>((v >> 8) & 0xFF));
  out->push_back(static_cast<char>((v >> 16) & 0xFF));
  out->push_back(static_cast<char>((v >> 24) & 0xFF));
}

bool ReadU32(const std::string& in, size_t* pos, uint32_t* v) {
  if (*pos + 4 > in.size()) return false;
  const auto* p = reinterpret_cast<const unsigned char*>(in.data() + *pos);
  *v = static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) |
       (static_cast<uint32_t>(p[2]) << 16) | (static_cast<uint32_t>(p[3]) << 24);
  *pos += 4;
  return true;
}

}  // namespace

bool IsBinaryRow(const std::string& stored) {
  return stored.size() >= 4 &&
         stored[0] == kBinaryMagic[0] && stored[1] == kBinaryMagic[1] &&
         stored[2] == kBinaryMagic[2] && stored[3] == kBinaryMagic[3];
}

Status EncodeRowBinary(const std::vector<ColumnDef>& cols,
                       const std::unordered_map<std::string, std::string>& values,
                       std::string* out) {
  if (!out) return Status::InvalidArgument("out is null");
  out->clear();
  out->append(kBinaryMagic, 4);
  AppendU32(out, static_cast<uint32_t>(cols.size()));
  for (const auto& col : cols) {
    const auto it = values.find(col.name);
    if (it == values.end()) continue;
    const std::string& name = col.name;
    const std::string& val = it->second;
    AppendU32(out, static_cast<uint32_t>(name.size()));
    out->append(name);
    AppendU32(out, static_cast<uint32_t>(val.size()));
    out->append(val);
  }
  return Status::Ok();
}

Status DecodeRowBinary(const std::string& stored,
                       std::unordered_map<std::string, std::string>* out) {
  if (!out) return Status::InvalidArgument("out is null");
  if (!IsBinaryRow(stored)) return Status::InvalidArgument("not binary row");
  out->clear();
  size_t pos = 4;
  uint32_t count = 0;
  if (!ReadU32(stored, &pos, &count)) return Status::CorruptPage("binary row trunc");
  for (uint32_t i = 0; i < count; ++i) {
    uint32_t name_len = 0;
    uint32_t val_len = 0;
    if (!ReadU32(stored, &pos, &name_len)) return Status::CorruptPage("binary row name");
    if (pos + name_len > stored.size()) return Status::CorruptPage("binary row name trunc");
    const std::string name = stored.substr(pos, name_len);
    pos += name_len;
    if (!ReadU32(stored, &pos, &val_len)) return Status::CorruptPage("binary row val");
    if (pos + val_len > stored.size()) return Status::CorruptPage("binary row val trunc");
    (*out)[name] = stored.substr(pos, val_len);
    pos += val_len;
  }
  return Status::Ok();
}

Status EncodeRowJson(const std::vector<ColumnDef>& cols,
                     const std::unordered_map<std::string, std::string>& values,
                     std::string* out) {
  if (!out) return Status::InvalidArgument("out is null");
  std::string json = "{";
  bool first = true;
  for (const auto& col : cols) {
    const auto it = values.find(col.name);
    if (it == values.end()) continue;
    if (!first) json.push_back(',');
    first = false;
    json += "\"" + JsonEscape(col.name) + "\":\"" + JsonEscape(it->second) + "\"";
  }
  if (first && !cols.empty()) {
    const auto it = values.find(cols[0].name);
    if (it != values.end()) {
      json += "\"" + JsonEscape(cols[0].name) + "\":\"" + JsonEscape(it->second) + "\"";
    }
  }
  json.push_back('}');
  *out = json;
  return Status::Ok();
}

Status DecodeRowJson(const std::string& json,
                     std::unordered_map<std::string, std::string>* out) {
  if (!out) return Status::InvalidArgument("out is null");
  out->clear();
  size_t pos = 0;
  while ((pos = json.find('"', pos)) != std::string::npos) {
    const auto k1 = pos;
    const auto k2 = json.find('"', k1 + 1);
    if (k2 == std::string::npos) break;
    const std::string key = json.substr(k1 + 1, k2 - k1 - 1);
    const auto colon = json.find(':', k2);
    if (colon == std::string::npos) break;
    const auto v1 = json.find('"', colon);
    if (v1 == std::string::npos) break;
    const auto v2 = json.find('"', v1 + 1);
    if (v2 == std::string::npos) break;
    (*out)[key] = json.substr(v1 + 1, v2 - v1 - 1);
    pos = v2 + 1;
  }
  return Status::Ok();
}

Status UpdateRowJson(const std::string& json, const std::string& col,
                     const std::string& value, std::string* out) {
  std::unordered_map<std::string, std::string> fields;
  const Status ds = DecodeRowJson(json, &fields);
  if (!ds.ok()) return ds;
  fields[col] = value;
  ColumnDef c{};
  c.name = col;
  return EncodeRowJson({c}, fields, out);
}

Status UpdateRowFields(const std::string& stored, const std::string& col,
                       const std::string& value, std::string* out) {
  std::unordered_map<std::string, std::string> fields;
  const Status ds = DecodeRowFields(stored, &fields);
  if (!ds.ok()) return ds;
  fields[col] = value;
  if (IsBinaryRow(stored)) {
    std::vector<ColumnDef> cols;
    cols.reserve(fields.size());
    for (const auto& kv : fields) {
      cols.push_back({kv.first, "TEXT"});
    }
    return EncodeRowBinary(cols, fields, out);
  }
  ColumnDef c{};
  c.name = col;
  return EncodeRowJson({c}, fields, out);
}

Status EncodeRow(const std::vector<ColumnDef>& cols, uint32_t schema_version,
                 const std::unordered_map<std::string, std::string>& values,
                 std::string* out) {
  if (schema_version >= 3) {
    return EncodeRowBinary(cols, values, out);
  }
  return EncodeRowJson(cols, values, out);
}

Status DecodeRowFields(const std::string& stored,
                       std::unordered_map<std::string, std::string>* out) {
  if (IsBinaryRow(stored)) return DecodeRowBinary(stored, out);
  return DecodeRowJson(stored, out);
}

Status DecodeRow(const std::string& stored,
                 std::unordered_map<std::string, std::string>* out) {
  return DecodeRowFields(stored, out);
}

}  // namespace sql
}  // namespace ebtree
