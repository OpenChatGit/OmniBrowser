#include "omni/bookmark_store.h"

#include <algorithm>
#include <chrono>
#include <fstream>
#include <filesystem>

#include "omni/paths.h"
#include "omni/utf8.h"

namespace omni::bookmarks {
namespace {

using Json = nlohmann::json;

Json LoadRaw() {
  const std::filesystem::path path(utf8::Widen(paths::BookmarksPath()));
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
  const std::filesystem::path path(utf8::Widen(paths::BookmarksPath()));
  std::ofstream out(path, std::ios::binary | std::ios::trunc);
  if (!out) {
    return;
  }
  out << entries.dump();
}

bool ValidUrl(const std::string& url) {
  return !url.empty() && url != "about:blank";
}

Json NormalizeEntry(const Json& entry) {
  if (!entry.is_object()) {
    return Json();
  }
  const std::string url = entry.value("url", std::string());
  if (!ValidUrl(url)) {
    return Json();
  }
  std::string title = entry.value("title", std::string());
  if (title.empty()) {
    title = url;
  }
  const int64_t ts = entry.value("ts", static_cast<int64_t>(0));
  return Json{{"url", url}, {"title", title}, {"ts", ts}};
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

Json Record(const std::string& url, const std::string& title, int64_t ts) {
  if (!ValidUrl(url)) {
    return List();
  }
  Json entries = List();
  Json filtered = Json::array();
  for (const auto& entry : entries) {
    if (entry.value("url", std::string()) != url) {
      filtered.push_back(entry);
    }
  }
  const int64_t now_ms = static_cast<int64_t>(
      std::chrono::duration_cast<std::chrono::milliseconds>(
          std::chrono::system_clock::now().time_since_epoch())
          .count());
  const int64_t stamp = ts > 0 ? ts : now_ms;
  filtered.insert(filtered.begin(),
                  Json{{"url", url},
                       {"title", title.empty() ? url : title},
                       {"ts", stamp}});
  Trim(filtered);
  SaveRaw(filtered);
  return filtered;
}

bool Remove(const std::string& url) {
  if (url.empty()) {
    return false;
  }
  Json entries = List();
  Json filtered = Json::array();
  bool removed = false;
  for (const auto& entry : entries) {
    if (entry.value("url", std::string()) == url) {
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

Json Import(const Json& entries) {
  if (!entries.is_array() || entries.empty()) {
    return List();
  }
  Json merged = List();
  for (const auto& raw : entries) {
    Json entry = NormalizeEntry(raw);
    if (entry.is_null()) {
      continue;
    }
    const std::string url = entry.value("url", std::string());
    const int64_t ts = entry.value("ts", 0LL);
    bool found = false;
    for (auto& existing : merged) {
      if (existing.value("url", std::string()) != url) {
        continue;
      }
      found = true;
      if (ts >= existing.value("ts", 0LL)) {
        existing = entry;
      }
      break;
    }
    if (!found) {
      merged.push_back(std::move(entry));
    }
  }
  SortByTsDesc(merged);
  Trim(merged);
  SaveRaw(merged);
  return merged;
}

}  // namespace omni::bookmarks
