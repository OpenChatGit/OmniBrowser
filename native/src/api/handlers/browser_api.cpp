#include "omni/api/register_apis.h"

#include "omni/api/api_dispatcher.h"
#include "omni/browser_commands.h"

namespace omni {
namespace {

void DispatchBrowserDomain(const ApiContext& ctx,
                           const Json& params,
                           ApiResponder& responder,
                           const std::string& method) {
  if (!ctx.owner || !ctx.cef_callback) {
    responder.Failure(500, "Browser API requires CEF context");
    return;
  }
  if (!HandleBrowserCommand(ctx.owner, ctx.browser, ctx.query_id,
                            ctx.persistent, method, params,
                            ctx.cef_callback)) {
    responder.Failure(404, "Unknown method: " + method);
  }
}

}  // namespace

void RegisterBrowserApis() {
  auto& api = ApiDispatcher::Get();
  const char* prefixes[] = {"browser.", "overlay.", "menu.",
                            "history.", "bookmarks.", "downloads."};
  for (const char* prefix : prefixes) {
    api.RegisterPrefix(
        prefix,
        [](const ApiContext& ctx, const Json& params, ApiResponder& responder) {
          DispatchBrowserDomain(ctx, params, responder, ctx.method);
        },
        ApiExposure::UiOnly);
  }
}

}  // namespace omni
