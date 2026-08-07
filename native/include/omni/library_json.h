#pragma once

#define JSON_NOEXCEPTION 1
#include "omni/game.h"
#include "omni/json.hpp"

namespace omni {

using Json = nlohmann::json;

Json GameToJson(const Game& game, bool running);
std::string BasenameStem(const std::string& path);

}  // namespace omni
