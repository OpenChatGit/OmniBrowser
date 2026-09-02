#pragma once

#include <string>

#include "omni/json.hpp"

namespace omni::session {

nlohmann::json Get();
bool Set(const nlohmann::json& session);
nlohmann::json ListTabs();
nlohmann::json GetTab(const std::string& tab_id);
std::string ActiveTabId();

}  // namespace omni::session
