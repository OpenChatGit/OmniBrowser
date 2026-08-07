#pragma once

#include <string>

#include "include/wrapper/cef_message_router.h"
#include "omni/library_json.h"

namespace omni {

class OmniHandler;

// Dispatches one library.* RPC. Returns true if handled.
bool HandleLibraryCommand(
    OmniHandler* owner,
    const std::string& method,
    const Json& params,
    CefRefPtr<CefMessageRouterBrowserSide::Callback> callback);

}  // namespace omni
