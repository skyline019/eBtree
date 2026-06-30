#include "catalog_store.h"

#include <filesystem>
#include <fstream>
#include <sstream>

namespace ebtree {
namespace sql {

namespace {

std::string JsonEscape(const std::string& s) {
  std::string out;
  out.reserve(s.size() + 4);
  out.push_back('"');
  for (char c : s) {
    if (c == '"') out += "\\\"";
    else if (c == '\\') out += "\\\\";
    else out.push_back(c);
  }
  out.push_back('"');
  return out;
}

bool ExtractJsonString(const std::string& line, const std::string& key,
                       std::string* out) {
  const std::string needle = "\"" + key + "\":";
  const auto pos = line.find(needle);
  if (pos == std::string::npos) return false;
  const auto q1 = line.find('"', pos + needle.size());
  if (q1 == std::string::npos) return false;
  const auto q2 = line.find('"', q1 + 1);
  if (q2 == std::string::npos) return false;
  *out = line.substr(q1 + 1, q2 - q1 - 1);
  return true;
}

bool ExtractJsonUint(const std::string& line, const std::string& key,
                     uint32_t* out) {
  const std::string needle = "\"" + key + "\":";
  const auto pos = line.find(needle);
  if (pos == std::string::npos) return false;
  *out = static_cast<uint32_t>(
      std::stoul(line.substr(pos + needle.size())));
  return true;
}

bool ExtractJsonBool(const std::string& line, const std::string& key,
                     bool* out) {
  const std::string needle = "\"" + key + "\":";
  const auto pos = line.find(needle);
  if (pos == std::string::npos) return false;
  const auto tail = line.substr(pos + needle.size());
  if (tail.find("true") == 0) {
    *out = true;
    return true;
  }
  if (tail.find("false") == 0) {
    *out = false;
    return true;
  }
  return false;
}

std::vector<std::string> ExtractJsonStringArray(const std::string& obj,
                                                const std::string& key) {
  std::vector<std::string> out;
  const auto cols_pos = obj.find("\"" + key + "\"");
  if (cols_pos == std::string::npos) return out;
  const auto arr_start = obj.find('[', cols_pos);
  const auto arr_end = obj.find(']', arr_start);
  if (arr_start == std::string::npos || arr_end == std::string::npos) return out;
  const std::string arr = obj.substr(arr_start, arr_end - arr_start + 1);
  size_t pos = 0;
  while ((pos = arr.find('"', pos)) != std::string::npos) {
    const auto q2 = arr.find('"', pos + 1);
    if (q2 == std::string::npos) break;
    out.push_back(arr.substr(pos + 1, q2 - pos - 1));
    pos = q2 + 1;
  }
  return out;
}

void LoadTablesFromJson(const std::string& content, Catalog* catalog) {
  size_t pos = 0;
  while ((pos = content.find("\"name\"", pos)) != std::string::npos) {
    const auto obj_start = content.rfind('{', pos);
    const auto obj_end = content.find('}', pos);
    if (obj_start == std::string::npos || obj_end == std::string::npos) break;
    const std::string obj = content.substr(obj_start, obj_end - obj_start + 1);
    if (obj.find("\"table\"") != std::string::npos &&
        obj.find("\"columns\"") != std::string::npos &&
        obj.find("\"key_column\"") == std::string::npos) {
      pos = obj_end + 1;
      continue;
    }
    std::string name;
    std::string key_col;
    std::string value_col;
    uint32_t id = 0;
    if (!ExtractJsonString(obj, "name", &name)) {
      pos = obj_end + 1;
      continue;
    }
    ExtractJsonString(obj, "key_column", &key_col);
    ExtractJsonString(obj, "value_column", &value_col);
    ExtractJsonUint(obj, "id", &id);

    std::vector<ColumnDef> cols;
    const auto cols_pos = obj.find("\"columns\"");
    if (cols_pos != std::string::npos) {
      const auto arr_start = obj.find('[', cols_pos);
      const auto arr_end = obj.find(']', arr_start);
      if (arr_start != std::string::npos && arr_end != std::string::npos) {
        const std::string arr = obj.substr(arr_start, arr_end - arr_start + 1);
        size_t col_pos = 0;
        while ((col_pos = arr.find("\"name\"", col_pos)) != std::string::npos) {
          const auto col_start = arr.rfind('{', col_pos);
          const auto col_end = arr.find('}', col_pos);
          if (col_start == std::string::npos || col_end == std::string::npos) break;
          const std::string col_obj =
              arr.substr(col_start, col_end - col_start + 1);
          std::string col_name;
          std::string col_type{"TEXT"};
          if (ExtractJsonString(col_obj, "name", &col_name)) {
            ExtractJsonString(col_obj, "type", &col_type);
            cols.push_back({col_name, col_type.empty() ? "TEXT" : col_type});
          }
          col_pos = col_end + 1;
        }
      }
    }
    if (cols.empty()) {
      cols.push_back({key_col.empty() ? "key" : key_col, "TEXT"});
      cols.push_back({value_col.empty() ? "value" : value_col, "TEXT"});
    }

    (void)catalog->RestoreTableV2(id, name, cols);
    pos = obj_end + 1;
  }
}

void LoadIndexesFromJson(const std::string& content, Catalog* catalog) {
  const auto idx_pos = content.find("\"indexes\"");
  if (idx_pos == std::string::npos) return;
  const auto arr_start = content.find('[', idx_pos);
  const auto arr_end = content.find(']', arr_start);
  if (arr_start == std::string::npos || arr_end == std::string::npos) return;
  const std::string arr = content.substr(arr_start, arr_end - arr_start + 1);
  size_t pos = 0;
  while ((pos = arr.find("\"name\"", pos)) != std::string::npos) {
    const auto obj_start = arr.rfind('{', pos);
    const auto obj_end = arr.find('}', pos);
    if (obj_start == std::string::npos || obj_end == std::string::npos) break;
    const std::string obj = arr.substr(obj_start, obj_end - obj_start + 1);
    std::string name;
    std::string table;
    uint32_t id = 0;
    bool unique = false;
    if (!ExtractJsonString(obj, "name", &name)) {
      pos = obj_end + 1;
      continue;
    }
    ExtractJsonString(obj, "table", &table);
    ExtractJsonUint(obj, "id", &id);
    ExtractJsonBool(obj, "unique", &unique);
    const auto columns = ExtractJsonStringArray(obj, "columns");
    if (columns.empty()) {
      pos = obj_end + 1;
      continue;
    }
    (void)catalog->RestoreIndex(id, name, table, columns, unique);
    pos = obj_end + 1;
  }
}

}  // namespace

std::string DefaultCatalogPath(const std::string& engine_path) {
  return (std::filesystem::path(engine_path) / "ebtree.catalog.json").string();
}

CatalogStore::CatalogStore(std::string path) : path_(std::move(path)) {}

Status CatalogStore::Load(Catalog* catalog) {
  if (!catalog) return Status::InvalidArgument("catalog is null");
  if (!std::filesystem::exists(path_)) return Status::Ok();

  std::ifstream in(path_);
  if (!in) return Status::IoError("cannot open catalog: " + path_);

  std::string content((std::istreambuf_iterator<char>(in)),
                      std::istreambuf_iterator<char>());
  catalog->Clear();
  LoadTablesFromJson(content, catalog);
  LoadIndexesFromJson(content, catalog);
  return Status::Ok();
}

Status CatalogStore::Save(const Catalog& catalog) const {
  const auto parent = std::filesystem::path(path_).parent_path();
  if (!parent.empty()) {
    std::filesystem::create_directories(parent);
  }
  std::ofstream out(path_, std::ios::trunc);
  if (!out) return Status::IoError("cannot write catalog: " + path_);

  out << "{\"v\":3,\"tables\":[";
  bool first = true;
  for (const auto& table : catalog.Tables()) {
    if (!first) out << ',';
    first = false;
    out << "{"
        << "\"id\":" << table.id << ","
        << "\"name\":" << JsonEscape(table.name) << ","
        << "\"key_column\":" << JsonEscape(table.key_column) << ","
        << "\"value_column\":" << JsonEscape(table.value_column) << ","
        << "\"schema_version\":" << table.schema_version << ","
        << "\"columns\":[";
    bool col_first = true;
    for (const auto& col : table.columns) {
      if (!col_first) out << ',';
      col_first = false;
      out << "{\"name\":" << JsonEscape(col.name) << ","
          << "\"type\":" << JsonEscape(col.type) << "}";
    }
    out << "]}";
  }
  out << "],\"indexes\":[";
  bool idx_first = true;
  for (const auto& idx : catalog.Indexes()) {
    if (!idx_first) out << ',';
    idx_first = false;
    out << "{"
        << "\"id\":" << idx.id << ","
        << "\"name\":" << JsonEscape(idx.name) << ","
        << "\"table\":" << JsonEscape(idx.table) << ","
        << "\"unique\":" << (idx.unique ? "true" : "false") << ","
        << "\"columns\":[";
    bool col_first = true;
    for (const auto& col : idx.columns) {
      if (!col_first) out << ',';
      col_first = false;
      out << JsonEscape(col);
    }
    out << "]}";
  }
  out << "]}\n";
  return Status::Ok();
}

}  // namespace sql
}  // namespace ebtree
