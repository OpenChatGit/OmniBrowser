#include "omni/library_ipc.h"

#include "include/wrapper/cef_helpers.h"
#include "omni/browser_commands.h"
#include "omni/git_commands.h"
#include "omni/library_commands.h"
#include "omni/omni_handler.h"
#include "omni/terminal_commands.h"
#include "omni/window_commands.h"

namespace omni {
namespace {

class LibraryIpcHandler : public CefMessageRouterBrowserSide::Handler {
 public:
  explicit LibraryIpcHandler(OmniHandler* owner) : owner_(owner) {}

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

    if (HandleLibraryCommand(owner_, method, params, callback)) {
      return true;
    }
    if (HandleWindowCommand(browser, method, params, callback)) {
      return true;
    }
    if (HandleTerminalCommand(owner_, query_id, persistent, method, params,
                              callback)) {
      return true;
    }
    if (HandleGitCommand(method, params, callback)) {
      return true;
    }
    if (HandleBrowserCommand(owner_, browser, query_id, persistent, method,
                             params, callback)) {
      return true;
    }

    callback->Failure(404, "Unknown method: " + method);
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
