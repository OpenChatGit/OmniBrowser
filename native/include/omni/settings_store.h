#pragma once

#include <string>

#include "omni/json.hpp"

namespace omni::settings {

nlohmann::json GetAll();
nlohmann::json Get(const std::string& key);
bool Set(const std::string& key, const nlohmann::json& value);
bool SetMany(const nlohmann::json& values);

}  // namespace omni::settings
