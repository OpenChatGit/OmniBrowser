#pragma once

#include "include/wrapper/cef_message_router.h"
#include "omni/json.hpp"

namespace omni {

using Json = nlohmann::json;

bool HandleGitCommand(
    const std::string& method,
    const Json& params,
    CefRefPtr<CefMessageRouterBrowserSide::Callback> callback);

}  // namespace omni
