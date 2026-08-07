#include "omni/download_store.h"

#include <algorithm>
#include <fstream>
#include <filesystem>

#include "omni/paths.h"
#include "omni/utf8.h"

namespace omni::downloads {
namespace {

using Json = nlohmann::json;

Json LoadRaw() {
  const std::filesystem::path path(utf8::Widen(paths::DownloadsPath()));
  std::ifstream in(path, std::ios::binary);
  if (!in) {
    return Json::array();
  }
  std::string data((std::istreambuf_iterator<char>(in)),
                   std::istreambuf_iterator<char>());
  if (data.empty()) {
    return Json::array();
  }
  Json parsed = Json::parse(data, nullptr, false);
  if (!parsed.is_array()) {
    return Json::array();
  }
  return parsed;
}

void SaveRaw(const Json& entries) {
  paths::EnsureAppDataDir();
  const std::filesystem::path path(utf8::Widen(paths::DownloadsPath()));
  std::ofstream out(path, std::ios::binary | std::ios::trunc);
  if (!out) {
    return;
  }
  out << entries.dump();
}

Json NormalizeEntry(const Json& entry) {
  if (!entry.is_object()) {
    return Json();
  }
  const std::string id = entry.value("id", std::string());
  if (id.empty()) {
    return Json();
  }
  std::string filename = entry.value("filename", std::string());
  const std::string path = entry.value("path", std::string());
  if (filename.empty() && !path.empty()) {
    const auto p = std::filesystem::path(utf8::Widen(path));
    filename = utf8::Narrow(p.filename().wstring());
  }
  if (filename.empty()) {
    filename = "download";
  }
  return Json{{"id", id},
              {"url", entry.value("url", std::string())},
              {"originalUrl", entry.value("originalUrl", std::string())},
              {"path", path},
              {"filename", filename},
              {"mime", entry.value("mime", std::string())},
              {"state", entry.value("state", std::string("complete"))},
              {"receivedBytes", entry.value("receivedBytes", 0LL)},
              {"totalBytes", entry.value("totalBytes", 0LL)},
              {"percent", entry.value("percent", 0)},
              {"ts", entry.value("ts", 0LL)}};
}

void Trim(Json& entries) {
  if (entries.size() > kMaxEntries) {
    entries.erase(entries.begin() + static_cast<std::ptrdiff_t>(kMaxEntries),
                  entries.end());
  }
}

void SortByTsDesc(Json& entries) {
  std::stable_sort(entries.begin(), entries.end(),
                   [](const Json& a, const Json& b) {
                     return a.value("ts", 0LL) > b.value("ts", 0LL);
                   });
}

}  // namespace

Json List() {
  Json entries = LoadRaw();
  Json out = Json::array();
  for (const auto& entry : entries) {
    Json normalized = NormalizeEntry(entry);
    if (!normalized.is_null()) {
      out.push_back(std::move(normalized));
    }
  }
  SortByTsDesc(out);
  Trim(out);
  return out;
}

Json Upsert(const Json& entry) {
  Json normalized = NormalizeEntry(entry);
  if (normalized.is_null()) {
    return List();
  }
  const std::string id = normalized.value("id", std::string());
  Json entries = List();
  bool found = false;
  for (auto& existing : entries) {
    if (existing.value("id", std::string()) != id) {
      continue;
    }
    existing = normalized;
    found = true;
    break;
  }
  if (!found) {
    entries.insert(entries.begin(), std::move(normalized));
  }
  SortByTsDesc(entries);
  Trim(entries);
  SaveRaw(entries);
  return entries;
}

bool Remove(const std::string& id) {
  if (id.empty()) {
    return false;
  }
  Json entries = List();
  Json filtered = Json::array();
  bool removed = false;
  for (const auto& entry : entries) {
    if (entry.value("id", std::string()) == id) {
      removed = true;
      continue;
    }
    filtered.push_back(entry);
  }
  if (removed) {
    SaveRaw(filtered);
  }
  return removed;
}

void Clear() {
  SaveRaw(Json::array());
}

}  // namespace omni::downloads
