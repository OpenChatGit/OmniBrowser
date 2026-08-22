#pragma once

#include "include/cef_client.h"
#include "include/views/cef_browser_view.h"
#include "include/views/cef_window.h"

namespace omni {

enum class BrowserPane {
  Shell,
  Content,
  Overlay,
  DevTools,
};

class OmniWindowDelegate : public CefWindowDelegate {
 public:
  OmniWindowDelegate(CefRefPtr<CefBrowserView> shell_view,
                     CefRefPtr<CefBrowserView> content_view,
                     CefRefPtr<CefBrowserView> overlay_view,
                     cef_runtime_style_t runtime_style);

  void OnWindowCreated(CefRefPtr<CefWindow> window) override;
  void OnWindowDestroyed(CefRefPtr<CefWindow> window) override;
  void OnWindowBoundsChanged(CefRefPtr<CefWindow> window,
                             const CefRect& new_bounds) override;
  bool CanClose(CefRefPtr<CefWindow> window) override;
  CefSize GetPreferredSize(CefRefPtr<CefView> view) override;
  cef_runtime_style_t GetWindowRuntimeStyle() override;

  bool IsFrameless(CefRefPtr<CefWindow> window) override;
  bool CanResize(CefRefPtr<CefWindow> window) override;
  bool CanMaximize(CefRefPtr<CefWindow> window) override;
  bool CanMinimize(CefRefPtr<CefWindow> window) override;

 private:
  CefRefPtr<CefBrowserView> shell_view_;
  CefRefPtr<CefBrowserView> content_view_;
  CefRefPtr<CefBrowserView> overlay_view_;
  const cef_runtime_style_t runtime_style_;
  IMPLEMENT_REFCOUNTING(OmniWindowDelegate);
};

// Separate top-level window for CEF DevTools. Must not share OmniWindowDelegate:
// that path is frameless and CanClose() shuts down the whole app.
class OmniDevToolsWindowDelegate : public CefWindowDelegate {
 public:
  OmniDevToolsWindowDelegate(CefRefPtr<CefBrowserView> view,
                             cef_runtime_style_t runtime_style);

  void OnWindowCreated(CefRefPtr<CefWindow> window) override;
  void OnWindowDestroyed(CefRefPtr<CefWindow> window) override;
  bool CanClose(CefRefPtr<CefWindow> window) override;
  CefSize GetPreferredSize(CefRefPtr<CefView> view) override;
  cef_runtime_style_t GetWindowRuntimeStyle() override;

  bool IsFrameless(CefRefPtr<CefWindow> window) override;
  bool CanResize(CefRefPtr<CefWindow> window) override;
  bool CanMaximize(CefRefPtr<CefWindow> window) override;
  bool CanMinimize(CefRefPtr<CefWindow> window) override;

 private:
  CefRefPtr<CefBrowserView> view_;
  const cef_runtime_style_t runtime_style_;
  IMPLEMENT_REFCOUNTING(OmniDevToolsWindowDelegate);
};

class OmniBrowserViewDelegate : public CefBrowserViewDelegate {
 public:
  OmniBrowserViewDelegate(cef_runtime_style_t runtime_style, BrowserPane pane);

  void OnBrowserCreated(CefRefPtr<CefBrowserView> browser_view,
                        CefRefPtr<CefBrowser> browser) override;
  void OnBrowserDestroyed(CefRefPtr<CefBrowserView> browser_view,
                          CefRefPtr<CefBrowser> browser) override;
  CefRefPtr<CefBrowserViewDelegate> GetDelegateForPopupBrowserView(
      CefRefPtr<CefBrowserView> browser_view,
      const CefBrowserSettings& settings,
      CefRefPtr<CefClient> client,
      bool is_devtools) override;
  bool OnPopupBrowserViewCreated(CefRefPtr<CefBrowserView> browser_view,
                                 CefRefPtr<CefBrowserView> popup_browser_view,
                                 bool is_devtools) override;
  cef_runtime_style_t GetBrowserRuntimeStyle() override;

  CefSize GetPreferredSize(CefRefPtr<CefView> view) override;
  CefSize GetMinimumSize(CefRefPtr<CefView> view) override;

 private:
  const cef_runtime_style_t runtime_style_;
  const BrowserPane pane_;
  IMPLEMENT_REFCOUNTING(OmniBrowserViewDelegate);
};

}  // namespace omni
