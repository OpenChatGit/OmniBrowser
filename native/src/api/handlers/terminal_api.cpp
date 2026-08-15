#include "omni/api/register_apis.h"

#include "omni/api/api_dispatcher.h"
#include "omni/terminal_commands.h"

namespace omni {

void RegisterTerminalApis() {
  ApiDispatcher::Get().RegisterPrefix(
      "terminal.",
      [](const ApiContext& ctx, const Json& params, ApiResponder& responder) {
        if (!ctx.owner || !ctx.cef_callback) {
          responder.Failure(500, "Terminal API requires CEF context");
          return;
        }
        if (!HandleTerminalCommand(ctx.owner, ctx.query_id, ctx.persistent,
                                   ctx.method, params, ctx.cef_callback)) {
          responder.Failure(404, "Unknown method: " + ctx.method);
        }
      },
      ApiExposure::UiOnly);
}

}  // namespace omni
