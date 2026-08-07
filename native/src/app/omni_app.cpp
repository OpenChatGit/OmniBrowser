#include "omni/omni_app.h"

#include "include/cef_browser.h"
#include "include/cef_command_line.h"
#include "include/views/cef_browser_view.h"
#include "include/views/cef_window.h"
#include "include/wrapper/cef_helpers.h"
#include "omni/dev_mode.h"
#include "omni/omni_handler.h"
#include "omni/paths.h"
#include "omni/window_delegate.h"

namespace omni {

void OmniApp::OnBeforeCommandLineProcessing(
    const CefString& process_type,
    CefRefPtr<CefCommandLine> command_line) {
  (void)process_type;
  // Keep media playing in hidden (background) tab browser views.
  command_line->AppendSwitch("disable-backgrounding-occluded-windows");
  command_line->AppendSwitch("disable-renderer-backgrounding");
  if (IsDevMode()) {
    // Avoid stale CSS/JS while editing from the source tree.
    command_line->AppendSwitch("disable-http-cache");
    command_line->AppendSwitchWithValue("disable-features", "IsolateOrigins");
  }
}

void OmniApp::OnContextInitialized() {
  CEF_REQUIRE_UI_THREAD();

  const bool alloy_style = true;
  const cef_runtime_style_t runtime_style = CEF_RUNTIME_STYLE_ALLOY;

  CefRefPtr<OmniHandler> handler(new OmniHandler(alloy_style));

  CefBrowserSettings browser_settings;
  const std::string url = paths::UiEntryUrl();

  CefRefPtr<OmniBrowserViewDelegate> shell_delegate(
      new OmniBrowserViewDelegate(runtime_style, BrowserPane::Shell));
  CefRefPtr<OmniBrowserViewDelegate> content_delegate(
      new OmniBrowserViewDelegate(runtime_style, BrowserPane::Content));

  CefRefPtr<CefBrowserView> shell_view = CefBrowserView::CreateBrowserView(
      handler, url, browser_settings, nullptr, nullptr, shell_delegate);

  CefBrowserSettings content_settings;
  CefRefPtr<CefBrowserView> content_view = CefBrowserView::CreateBrowserView(
      handler, "about:blank", content_settings, nullptr, nullptr,
      content_delegate);

  // Dropdown overlay pane: floats above shell + content via
  // CefOverlayController, so menus never push the content view around.
  CefRefPtr<OmniBrowserViewDelegate> overlay_delegate(
      new OmniBrowserViewDelegate(runtime_style, BrowserPane::Overlay));
  CefBrowserSettings overlay_settings;
  // Windowed CEF browsers cannot composite true transparency (transparent
  // alpha clears to white). Match Brave: opaque menu surface, sized to fit.
  overlay_settings.background_color = CefColorSetARGB(255, 0x2a, 0x29, 0x2a);
  CefRefPtr<CefBrowserView> overlay_view = CefBrowserView::CreateBrowserView(
      handler, paths::UiOverlayUrl(), overlay_settings, nullptr, nullptr,
      overlay_delegate);

  handler->SetShellBrowserView(shell_view);
  handler->SetContentBrowserView(content_view);
  handler->SetOverlayBrowserView(overlay_view);

  CefWindow::CreateTopLevelWindow(new OmniWindowDelegate(
      shell_view, content_view, overlay_view, runtime_style));
}

CefRefPtr<CefClient> OmniApp::GetDefaultClient() {
  return OmniHandler::GetInstance();
}

bool OmniApp::OnAlreadyRunningAppRelaunch(
    CefRefPtr<CefCommandLine> command_line,
    const CefString& current_directory) {
  (void)command_line;
  (void)current_directory;

  // Single-instance lock is tied to root_cache_path — bring the existing
  // window forward instead of opening a second profile.
  if (auto* handler = OmniHandler::GetInstance()) {
    if (auto shell = handler->shell_browser_view()) {
      if (auto window = shell->GetWindow()) {
        window->Show();
        window->Activate();
        return true;
      }
    }
  }
  return true;
}

void OmniApp::OnWebKitInitialized() {
  CefMessageRouterConfig config;
  renderer_router_ = CefMessageRouterRendererSide::Create(config);
}

void OmniApp::OnContextCreated(CefRefPtr<CefBrowser> browser,
                               CefRefPtr<CefFrame> frame,
                               CefRefPtr<CefV8Context> context) {
  renderer_router_->OnContextCreated(browser, frame, context);
}

void OmniApp::OnContextReleased(CefRefPtr<CefBrowser> browser,
                                CefRefPtr<CefFrame> frame,
                                CefRefPtr<CefV8Context> context) {
  renderer_router_->OnContextReleased(browser, frame, context);
}

bool OmniApp::OnProcessMessageReceived(CefRefPtr<CefBrowser> browser,
                                       CefRefPtr<CefFrame> frame,
                                       CefProcessId source_process,
                                       CefRefPtr<CefProcessMessage> message) {
  return renderer_router_->OnProcessMessageReceived(browser, frame,
                                                    source_process, message);
}

}  // namespace omni
