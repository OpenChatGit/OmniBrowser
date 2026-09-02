#include "omni/plugin_registry.h"

#include <filesystem>
#include <fstream>

#include "omni/paths.h"
#include "omni/settings_store.h"
#include "omni/utf8.h"

namespace omni::plugins {
namespace {

using Json = nlohmann::json;

constexpr const char* kEnabledKey = "plugins.enabled";

Json EnabledMap() {
  const Json stored = settings::Get(kEnabledKey);
  if (stored.is_object()) {
    return stored;
  }
  return Json::object();
}

void SaveEnabledMap(const Json& map) {
  settings::Set(kEnabledKey, map);
}

Json ReadManifest(const std::filesystem::path& manifest_path) {
  std::ifstream in(manifest_path, std::ios::binary);
  if (!in) {
    return Json();
  }
  std::string data((std::istreambuf_iterator<char>(in)),
                   std::istreambuf_iterator<char>());
  if (data.empty()) {
    return Json();
  }
  Json parsed = Json::parse(data, nullptr, false);
  if (!parsed.is_object()) {
    return Json();
  }
  return parsed;
}

}  // namespace

Json ListInstalled() {
  Json enabled = EnabledMap();
  Json out = Json::array();

  const std::filesystem::path root(utf8::Widen(paths::PluginsDir()));
  std::error_code ec;
  if (!std::filesystem::exists(root, ec)) {
    return out;
  }

  for (const auto& entry : std::filesystem::directory_iterator(root, ec)) {
    if (!entry.is_directory()) {
      continue;
    }
    const auto manifest = entry.path() / "manifest.json";
    if (!std::filesystem::exists(manifest, ec)) {
      continue;
    }
    Json manifest_json = ReadManifest(manifest);
    if (manifest_json.is_discarded() || !manifest_json.is_object()) {
      continue;
    }
    const std::string id = manifest_json.value("id", entry.path().filename().string());
    if (id.empty()) {
      continue;
    }
    manifest_json["id"] = id;
    manifest_json["path"] = utf8::Narrow(entry.path().wstring());
    const auto enabled_it = enabled.find(id);
    const bool is_enabled =
        enabled_it != enabled.end() ? enabled_it->get<bool>() : true;
    manifest_json["enabled"] = is_enabled;
    out.push_back(std::move(manifest_json));
  }
  return out;
}

bool SetEnabled(const std::string& id, bool enabled) {
  if (id.empty()) {
    return false;
  }
  Json map = EnabledMap();
  map[id] = enabled;
  SaveEnabledMap(map);
  return true;
}

}  // namespace omni::plugins
