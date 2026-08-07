#pragma once

#include <string>

#include "omni/json.hpp"

namespace omni::bookmarks {

constexpr size_t kMaxEntries = 500;

nlohmann::json List();
nlohmann::json Record(const std::string& url,
                      const std::string& title,
                      int64_t ts);
bool Remove(const std::string& url);
void Clear();
nlohmann::json Import(const nlohmann::json& entries);

}  // namespace omni::bookmarks
