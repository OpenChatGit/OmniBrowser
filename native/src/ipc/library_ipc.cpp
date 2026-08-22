#include "omni/library_ipc.h"

#include "include/wrapper/cef_helpers.h"
#include "omni/api/api_access.h"
#include "omni/api/api_dispatcher.h"
#include "omni/api/register_apis.h"
#include "omni/omni_handler.h"

namespace omni {
namespace {

class LibraryIpcHandler : public CefMessageRouterBrowserSide::Handler {
 public:
  explicit LibraryIpcHandler(OmniHandler* owner) : owner_(owner) {
    RegisterAllApis();
  }

  bool OnQuery(CefRefPtr<CefBrowser> browser,
               CefRefPtr<CefFrame> frame,
               int64_t query_id,
               const CefString& request,
               bool persistent,
               CefRefPtr<Callback> callback) override {
    CEF_REQUIRE_UI_THREAD();
    (void)frame;

    const Json req = Json::parse(request.ToString(), nullptr, false);
    if (req.is_discarded() || !req.is_object()) {
      callback->Failure(400, "Invalid JSON");
      return true;
    }

    const std::string method = req.value("method", "");
    const Json params = req.contains("params") && req["params"].is_object()
                            ? req["params"]
                            : Json::object();

    ApiContext ctx;
    ctx.owner = owner_;
    ctx.browser = browser;
    ctx.method = method;
    ctx.query_id = query_id;
    ctx.persistent = persistent;
    ctx.cef_callback = callback;

    if (!ApiAccessAllowed(ctx)) {
      callback->Failure(403, "Forbidden: " + method);
      return true;
    }

    CefApiResponder responder(callback);
    if (!ApiDispatcher::Get().Dispatch(method, ctx, params, responder)) {
      callback->Failure(404, "Unknown method: " + method);
    }
    return true;
  }

  void OnQueryCanceled(CefRefPtr<CefBrowser> browser,
                       CefRefPtr<CefFrame> frame,
                       int64_t query_id) override {
    CEF_REQUIRE_UI_THREAD();
    (void)browser;
    (void)frame;
    if (owner_) {
      owner_->terminals().UnsubscribeByQuery(query_id);
      owner_->UnsubscribeBrowserEvents(query_id);
      owner_->UnsubscribeOverlayEvents(query_id);
    }
  }

 private:
  OmniHandler* owner_;
};

}  // namespace

CefMessageRouterBrowserSide::Handler* CreateLibraryIpcHandler(
    OmniHandler* handler) {
  return new LibraryIpcHandler(handler);
}

}  // namespace omni
