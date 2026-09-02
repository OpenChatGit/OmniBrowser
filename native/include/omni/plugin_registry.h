#pragma once

#include "omni/json.hpp"

namespace omni::plugins {

nlohmann::json ListInstalled();
bool SetEnabled(const std::string& id, bool enabled);

}  // namespace omni::plugins
