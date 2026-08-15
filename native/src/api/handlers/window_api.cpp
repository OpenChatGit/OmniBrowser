#include "omni/api/register_apis.h"

#include "omni/api/api_dispatcher.h"
#include "omni/window_commands.h"

namespace omni {

void RegisterWindowApis() {
  ApiDispatcher::Get().RegisterPrefix(
      "window.",
      [](const ApiContext& ctx, const Json& params, ApiResponder& responder) {
        if (!ctx.cef_callback) {
          responder.Failure(500, "Window API requires CEF context");
          return;
        }
        if (!HandleWindowCommand(ctx.browser, ctx.method, params,
                                 ctx.cef_callback)) {
          // Also covers tab.* and app.info via window_commands.
          responder.Failure(404, "Unknown method: " + ctx.method);
        }
      },
      ApiExposure::UiOnly);

  ApiDispatcher::Get().RegisterPrefix(
      "tab.",
      [](const ApiContext& ctx, const Json& params, ApiResponder& responder) {
        if (!ctx.cef_callback) {
          responder.Failure(500, "Tab API requires CEF context");
          return;
        }
        if (!HandleWindowCommand(ctx.browser, ctx.method, params,
                                 ctx.cef_callback)) {
          responder.Failure(404, "Unknown method: " + ctx.method);
        }
      },
      ApiExposure::UiOnly);

  ApiDispatcher::Get().Register(
      "app.info",
      [](const ApiContext& ctx, const Json& params, ApiResponder& responder) {
        if (!ctx.cef_callback) {
          responder.Failure(500, "app.info requires CEF context");
          return;
        }
        if (!HandleWindowCommand(ctx.browser, ctx.method, params,
                                 ctx.cef_callback)) {
          responder.Failure(404, "Unknown method: " + ctx.method);
        }
        (void)params;
      },
      ApiExposure::UiOnly);
}

}  // namespace omni
