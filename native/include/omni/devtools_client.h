#pragma once

#include <functional>
#include <string>

#include "include/cef_browser.h"
#include "include/cef_devtools_message_observer.h"
#include "include/wrapper/cef_helpers.h"
#include "omni/library_json.h"

namespace omni {

using DevToolsCallback = std::function<void(bool ok, Json payload)>;

// Browser-process CDP session (no DevTools UI). Runtime.evaluate and
// DOM.setFileInputFiles are not subject to page CSP.
class DevToolsClient : public CefDevToolsMessageObserver {
 public:
  static DevToolsClient& Get();

  void Attach(CefRefPtr<CefBrowser> browser);
  void Call(CefRefPtr<CefBrowser> browser,
            const std::string& method,
            const Json& params,
            DevToolsCallback callback);
  Json TakeFileChooser(int browser_id);
  void FailAll(const std::string& reason);
  void Detach(int browser_id);

  bool OnDevToolsMessage(CefRefPtr<CefBrowser> browser,
                         const void* message,
                         size_t message_size) override;
  void OnDevToolsAgentDetached(CefRefPtr<CefBrowser> browser) override;

 private:
  DevToolsClient() = default;

  IMPLEMENT_REFCOUNTING(DevToolsClient);
};

}  // namespace omni
