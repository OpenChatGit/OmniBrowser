#pragma once

#include "include/wrapper/cef_message_router.h"

namespace omni {

class OmniHandler;

// Handles cefQuery JSON RPC for the game library.
CefMessageRouterBrowserSide::Handler* CreateLibraryIpcHandler(
    OmniHandler* handler);

}  // namespace omni
