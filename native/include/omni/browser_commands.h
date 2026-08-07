#pragma once

#include <string>

#include "include/cef_browser.h"
#include "include/wrapper/cef_message_router.h"
#include "omni/library_json.h"

namespace omni {

class OmniHandler;

bool HandleBrowserCommand(
    OmniHandler* owner,
    CefRefPtr<CefBrowser> browser,
    int64_t query_id,
    bool persistent,
    const std::string& method,
    const Json& params,
    CefRefPtr<CefMessageRouterBrowserSide::Callback> callback);

}  // namespace omni
