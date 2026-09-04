#include "omni/api/register_apis.h"

#include "omni/api/api_dispatcher.h"
#include "omni/appearance.h"
#include "omni/omni_handler.h"
#include "omni/plugin_registry.h"
#include "omni/session_store.h"
#include "omni/settings_store.h"

namespace omni {
namespace {

bool IsShellOrOverlay(const ApiContext& ctx) {
  return ctx.owner && ctx.browser &&
         (ctx.owner->IsShellBrowser(ctx.browser) ||
          ctx.owner->IsOverlayBrowser(ctx.browser));
}

}  // namespace

void RegisterPlatformApis() {
  auto& api = ApiDispatcher::Get();

  api.Register(
      "api.list",
      [](const ApiContext& ctx, const Json& params, ApiResponder& responder) {
        (void)ctx;
        (void)params;
        responder.Success(
            Json{{"ok", true}, {"methods", ApiDispatcher::Get().ListCatalog()}}
                .dump());
      },
      ApiExposure::UiOnly);

  api.Register(
      "settings.get",
      [](const ApiContext& ctx, const Json& params, ApiResponder& responder) {
        (void)ctx;
        const std::string key = params.value("key", "");
        if (key.empty()) {
          responder.Success(
              Json{{"ok", true}, {"settings", settings::GetAll()}}.dump());
          return;
        }
        if (key == "appearance") {
          responder.Success(
              Json{{"ok", true},
                   {"key", key},
                   {"value", AppearancePreference()},
                   {"theme", ChromeShouldUseDark() ? "dark" : "light"}}
                  .dump());
          return;
        }
        responder.Success(
            Json{{"ok", true}, {"key", key}, {"value", settings::Get(key)}}
                .dump());
      },
      ApiExposure::UiOnly);

  api.Register(
      "settings.set",
      [](const ApiContext& ctx, const Json& params, ApiResponder& responder) {
        if (!IsShellOrOverlay(ctx)) {
          responder.Failure(403, "settings.set requires shell or overlay");
          return;
        }
        if (params.contains("settings") && params["settings"].is_object()) {
          const bool ok = settings::SetMany(params["settings"]);
          if (ok && params["settings"].contains("appearance")) {
            ApplyChromeAppearance();
          }
          responder.Success(Json{{"ok", ok}}.dump());
          return;
        }
        const std::string key = params.value("key", "");
        if (key.empty() || !params.contains("value")) {
          responder.Failure(400, "key and value required");
          return;
        }
        const bool ok = settings::Set(key, params["value"]);
        if (ok && key == "appearance") {
          ApplyChromeAppearance();
        }
        responder.Success(Json{{"ok", ok}, {"key", key}}.dump());
      },
      ApiExposure::UiOnly);

  api.Register(
      "session.get",
      [](const ApiContext& ctx, const Json& params, ApiResponder& responder) {
        (void)ctx;
        (void)params;
        const Json session = session::Get();
        responder.Success(
            Json{{"ok", true},
                 {"session", session},
                 {"activeId", session.value("activeId", std::string())}}
                .dump());
      },
      ApiExposure::UiOnly);

  api.Register(
      "session.set",
      [](const ApiContext& ctx, const Json& params, ApiResponder& responder) {
        if (!IsShellOrOverlay(ctx)) {
          responder.Failure(403, "session.set requires shell or overlay");
          return;
        }
        const Json session =
            params.contains("session") && params["session"].is_object()
                ? params["session"]
                : params;
        if (!session.is_object()) {
          responder.Failure(400, "session object required");
          return;
        }
        const bool ok = session::Set(session);
        responder.Success(Json{{"ok", ok}}.dump());
      },
      ApiExposure::UiOnly);

  api.Register(
      "tabs.list",
      [](const ApiContext& ctx, const Json& params, ApiResponder& responder) {
        (void)ctx;
        (void)params;
        responder.Success(
            Json{{"ok", true},
                 {"tabs", session::ListTabs()},
                 {"activeId", session::ActiveTabId()}}
                .dump());
      },
      ApiExposure::UiOnly);

  api.Register(
      "tabs.get",
      [](const ApiContext& ctx, const Json& params, ApiResponder& responder) {
        (void)ctx;
        const std::string tab_id = params.value("tabId", "");
        if (tab_id.empty()) {
          responder.Failure(400, "tabId required");
          return;
        }
        const Json tab = session::GetTab(tab_id);
        if (tab.is_null() || tab.empty()) {
          responder.Failure(404, "Tab not found");
          return;
        }
        responder.Success(Json{{"ok", true}, {"tab", tab}}.dump());
      },
      ApiExposure::UiOnly);

  api.Register(
      "plugins.list",
      [](const ApiContext& ctx, const Json& params, ApiResponder& responder) {
        (void)ctx;
        (void)params;
        responder.Success(
            Json{{"ok", true}, {"plugins", plugins::ListInstalled()}}.dump());
      },
      ApiExposure::UiOnly);

  api.Register(
      "plugins.register",
      [](const ApiContext& ctx, const Json& params, ApiResponder& responder) {
        if (!IsShellOrOverlay(ctx)) {
          responder.Failure(403, "plugins.register requires shell or overlay");
          return;
        }
        const std::string id = params.value("id", "");
        if (id.empty()) {
          responder.Failure(400, "id required");
          return;
        }
        const bool enabled = params.value("enabled", true);
        const bool ok = plugins::SetEnabled(id, enabled);
        responder.Success(
            Json{{"ok", ok}, {"id", id}, {"enabled", enabled}}.dump());
      },
      ApiExposure::UiOnly);
}

}  // namespace omni
