#include "omni/settings_store.h"

#include <fstream>
#include <filesystem>

#include "omni/paths.h"
#include "omni/utf8.h"

namespace omni::settings {
namespace {

using Json = nlohmann::json;

Json LoadRaw() {
  const std::filesystem::path path(utf8::Widen(paths::SettingsPath()));
  std::ifstream in(path, std::ios::binary);
  if (!in) {
    return Json::object();
  }
  std::string data((std::istreambuf_iterator<char>(in)),
                   std::istreambuf_iterator<char>());
  if (data.empty()) {
    return Json::object();
  }
  Json parsed = Json::parse(data, nullptr, false);
  if (!parsed.is_object()) {
    return Json::object();
  }
  return parsed;
}

void SaveRaw(const Json& root) {
  if (!root.is_object()) {
    return;
  }
  paths::EnsureAppDataDir();
  const std::filesystem::path path(utf8::Widen(paths::SettingsPath()));
  std::ofstream out(path, std::ios::binary | std::ios::trunc);
  if (!out) {
    return;
  }
  out << root.dump();
}

}  // namespace

Json GetAll() {
  return LoadRaw();
}

Json Get(const std::string& key) {
  if (key.empty()) {
    return GetAll();
  }
  const Json root = LoadRaw();
  if (!root.contains(key)) {
    return Json();
  }
  return root[key];
}

bool Set(const std::string& key, const Json& value) {
  if (key.empty()) {
    return false;
  }
  Json root = LoadRaw();
  root[key] = value;
  SaveRaw(root);
  return true;
}

bool SetMany(const Json& values) {
  if (!values.is_object()) {
    return false;
  }
  Json root = LoadRaw();
  for (auto it = values.begin(); it != values.end(); ++it) {
    root[it.key()] = it.value();
  }
  SaveRaw(root);
  return true;
}

}  // namespace omni::settings
