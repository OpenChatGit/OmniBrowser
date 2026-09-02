#include "omni/api/register_apis.h"

#include "omni/adblock_service.h"
#include "omni/api/api_dispatcher.h"

namespace omni {

void RegisterAdblockApis() {
  auto& api = ApiDispatcher::Get();

  api.Register(
      "browser.adblock.get",
      [](const ApiContext& ctx, const Json& params, ApiResponder& responder) {
        (void)ctx;
        const std::string host = params.value("host", "");
        responder.Success(AdblockService::Get().StatsJson(host).dump());
      },
      ApiExposure::UiOnly);

  api.Register(
      "browser.adblock.set",
      [](const ApiContext& ctx, const Json& params, ApiResponder& responder) {
        (void)ctx;
        AdblockService::Get().ApplyPrefs(params);
        // ApplyPrefs() already calls LoadListsIntoEngineLocked() when the
        // "aggressive" flag changes — calling ReloadEngine() afterwards was a
        // second full list-reload. Only honour an explicit "reload" request.
        if (params.value("reload", false)) {
          AdblockService::Get().ReloadEngine();
        }
        const std::string host = params.value("host", "");
        responder.Success(AdblockService::Get().StatsJson(host).dump());
      },
      ApiExposure::UiOnly);

  api.Register(
      "browser.adblock.allowlist",
      [](const ApiContext& ctx, const Json& params, ApiResponder& responder) {
        (void)ctx;
        const std::string host = params.value("host", "");
        const bool allow = params.value("allow", true);
        AdblockService::Get().AllowlistHost(host, allow);
        responder.Success(AdblockService::Get().StatsJson(host).dump());
      },
      ApiExposure::UiOnly);

  api.Register(
      "browser.adblock.cosmetics",
      [](const ApiContext& ctx, const Json& params, ApiResponder& responder) {
        (void)ctx;
        const std::string url = params.value("url", "");
        if (url.empty()) {
          responder.Failure(400, "url required");
          return;
        }
        const auto cosmetics = AdblockService::Get().CosmeticsForUrl(url);
        responder.Success(Json{{"hideCss", cosmetics.hide_css},
                               {"injectedScript", cosmetics.injected_script},
                               {"exceptions", cosmetics.exceptions_json},
                               {"generichide", cosmetics.generichide}}
                              .dump());
      },
      ApiExposure::UiOnly);

  api.Register(
      "browser.adblock.classId",
      [](const ApiContext& ctx, const Json& params, ApiResponder& responder) {
        (void)ctx;
        std::vector<std::string> classes;
        std::vector<std::string> ids;
        if (params.contains("classes") && params["classes"].is_array()) {
          for (const auto& c : params["classes"]) {
            if (c.is_string()) {
              classes.push_back(c.get<std::string>());
            }
          }
        }
        if (params.contains("ids") && params["ids"].is_array()) {
          for (const auto& id : params["ids"]) {
            if (id.is_string()) {
              ids.push_back(id.get<std::string>());
            }
          }
        }
        std::string exceptions = "[]";
        if (params.contains("exceptions")) {
          if (params["exceptions"].is_string()) {
            exceptions = params["exceptions"].get<std::string>();
          } else {
            exceptions = params["exceptions"].dump();
          }
        }
        const std::string css = AdblockService::Get().HiddenClassIdCss(
            classes, ids, exceptions);
        responder.Success(Json{{"hideCss", css}}.dump());
      },
      ApiExposure::UiOnly);
}

}  // namespace omni
