#pragma once

#include <string>

#include "omni/json.hpp"

namespace omni::downloads {

constexpr size_t kMaxEntries = 200;

nlohmann::json List();
nlohmann::json Upsert(const nlohmann::json& entry);
bool Remove(const std::string& id);
void Clear();

}  // namespace omni::downloads
