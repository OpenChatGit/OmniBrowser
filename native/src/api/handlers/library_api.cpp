#include "omni/api/register_apis.h"

#include "omni/api/api_dispatcher.h"
#include "omni/library_commands.h"

namespace omni {

void RegisterLibraryApis() {
  ApiDispatcher::Get().RegisterPrefix(
      "library.",
      [](const ApiContext& ctx, const Json& params, ApiResponder& responder) {
        if (!ctx.owner || !ctx.cef_callback) {
          responder.Failure(500, "Library API requires CEF context");
          return;
        }
        if (!HandleLibraryCommand(ctx.owner, ctx.method, params,
                                  ctx.cef_callback)) {
          responder.Failure(404, "Unknown method: " + ctx.method);
        }
      },
      ApiExposure::UiOnly);
}

}  // namespace omni
