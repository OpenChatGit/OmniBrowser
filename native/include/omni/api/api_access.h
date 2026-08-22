#pragma once

#include "omni/api/api_dispatcher.h"

namespace omni {

/** Returns true when the caller may invoke this RPC method. */
bool ApiAccessAllowed(const ApiContext& ctx);

/** Content browser loading a bundled ui/ page (history.html, etc.). */
bool IsTrustedUiContentBrowser(OmniHandler* owner,
                               CefRefPtr<CefBrowser> browser);

}  // namespace omni
