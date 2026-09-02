#include "omni/session_store.h"

#include <fstream>
#include <filesystem>

#include "omni/paths.h"
#include "omni/utf8.h"

namespace omni::session {
namespace {

using Json = nlohmann::json;

Json LoadRaw() {
  const std::filesystem::path path(utf8::Widen(paths::SessionPath()));
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

void SaveRaw(const Json& session) {
  if (!session.is_object()) {
    return;
  }
  paths::EnsureAppDataDir();
  const std::filesystem::path path(utf8::Widen(paths::SessionPath()));
  std::ofstream out(path, std::ios::binary | std::ios::trunc);
  if (!out) {
    return;
  }
  out << session.dump();
}

}  // namespace

Json Get() {
  return LoadRaw();
}

bool Set(const Json& session) {
  if (!session.is_object()) {
    return false;
  }
  SaveRaw(session);
  return true;
}

Json ListTabs() {
  const Json session = LoadRaw();
  if (session.contains("tabs") && session["tabs"].is_array()) {
    return session["tabs"];
  }
  return Json::array();
}

Json GetTab(const std::string& tab_id) {
  if (tab_id.empty()) {
    return Json();
  }
  const Json tabs = ListTabs();
  for (const auto& tab : tabs) {
    if (tab.is_object() && tab.value("id", std::string()) == tab_id) {
      return tab;
    }
  }
  return Json();
}

std::string ActiveTabId() {
  const Json session = LoadRaw();
  return session.value("activeId", std::string());
}

}  // namespace omni::session
