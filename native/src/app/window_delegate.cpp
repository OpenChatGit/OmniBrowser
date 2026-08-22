#include "omni/window_delegate.h"

#include "include/views/cef_box_layout.h"
#include "include/views/cef_fill_layout.h"
#include "include/views/cef_overlay_controller.h"
#include "omni/omni_handler.h"

namespace omni {
namespace {

constexpr int kMinChromeHeight = 80;

int CurrentChromeHeight() {
  auto* handler = OmniHandler::GetInstance();
  const int h = handler ? handler->chrome_height() : kMinChromeHeight;
  return h >= kMinChromeHeight ? h : kMinChromeHeight;
}

bool ContentIsVisible() {
  auto* handler = OmniHandler::GetInstance();
  return handler && handler->content_visible();
}

}  // namespace

OmniWindowDelegate::OmniWindowDelegate(CefRefPtr<CefBrowserView> shell_view,
                                       CefRefPtr<CefBrowserView> content_view,
                                       CefRefPtr<CefBrowserView> overlay_view,
                                       cef_runtime_style_t runtime_style)
    : shell_view_(shell_view),
      content_view_(content_view),
      overlay_view_(overlay_view),
      runtime_style_(runtime_style) {}

void OmniWindowDelegate::OnWindowCreated(CefRefPtr<CefWindow> window) {
  // Vertical BoxLayout on the window itself: shell strip on top, content
  // below. Never use FillLayout with two children — it stacks them fullscreen.
  CefBoxLayoutSettings settings;
  settings.horizontal = 0;
  settings.between_child_spacing = 0;
  settings.default_flex = 0;
  settings.cross_axis_alignment = CEF_AXIS_ALIGNMENT_STRETCH;
  CefRefPtr<CefBoxLayout> layout = window->SetToBoxLayout(settings);

  window->AddChildView(shell_view_);
  if (content_view_) {
    window->AddChildView(content_view_);
    content_view_->SetVisible(false);
  }

  if (layout) {
    // Start mode: shell soaks up all space; content is hidden.
    layout->SetFlexForView(shell_view_, 1);
    if (content_view_) {
      layout->SetFlexForView(content_view_, 1);
    }
  }

  // Dropdown overlay: floats above both browser views with its own z-order,
  // so menus never resize/push the content pane. Added last so it stacks
  // on top of all child views.
  if (overlay_view_) {
    CefRefPtr<CefOverlayController> overlay_controller =
        window->AddOverlayView(overlay_view_, CEF_DOCKING_MODE_CUSTOM,
                               /*can_activate=*/true);
    if (overlay_controller) {
      overlay_controller->SetVisible(false);
    }
    if (auto* handler = OmniHandler::GetInstance()) {
      handler->SetOverlayController(overlay_controller);
    }
  }

  window->Show();

  if (auto* handler = OmniHandler::GetInstance()) {
    handler->LayoutContentBrowser();
  }
}

void OmniWindowDelegate::OnWindowDestroyed(CefRefPtr<CefWindow> window) {
  (void)window;
  if (auto* handler = OmniHandler::GetInstance()) {
    handler->SetOverlayController(nullptr);
    handler->CancelAppMenu();
  }
  shell_view_ = nullptr;
  content_view_ = nullptr;
  overlay_view_ = nullptr;
}

void OmniWindowDelegate::OnWindowBoundsChanged(CefRefPtr<CefWindow> window,
                                               const CefRect& new_bounds) {
  (void)window;
  (void)new_bounds;
  if (auto* handler = OmniHandler::GetInstance()) {
    // Anchors are stale after a resize; dismiss tip overlay + native menu.
    // Do NOT call LayoutContentBrowser / WasResized here — BoxLayout already
    // resizes the content pane. Extra WasResized mid-drag makes sites like
    // YouTube Shorts treat the viewport jump as a scroll and change videos.
    handler->OverlayHide();
    handler->CancelAppMenu();
  }
}

bool OmniWindowDelegate::CanClose(CefRefPtr<CefWindow> window) {
  (void)window;
  if (auto* handler = OmniHandler::GetInstance()) {
    handler->BeginShutdown();
  }
  CefRefPtr<CefBrowser> browser =
      shell_view_ ? shell_view_->GetBrowser() : nullptr;
  if (browser) {
    return browser->GetHost()->TryCloseBrowser();
  }
  return true;
}

CefSize OmniWindowDelegate::GetPreferredSize(CefRefPtr<CefView> view) {
  (void)view;
  return CefSize(1280, 800);
}

cef_runtime_style_t OmniWindowDelegate::GetWindowRuntimeStyle() {
  return runtime_style_;
}

bool OmniWindowDelegate::IsFrameless(CefRefPtr<CefWindow> window) {
  (void)window;
  return true;
}

bool OmniWindowDelegate::CanResize(CefRefPtr<CefWindow> window) {
  (void)window;
  return true;
}

bool OmniWindowDelegate::CanMaximize(CefRefPtr<CefWindow> window) {
  (void)window;
  return true;
}

bool OmniWindowDelegate::CanMinimize(CefRefPtr<CefWindow> window) {
  (void)window;
  return true;
}

OmniDevToolsWindowDelegate::OmniDevToolsWindowDelegate(
    CefRefPtr<CefBrowserView> view,
    cef_runtime_style_t runtime_style)
    : view_(view), runtime_style_(runtime_style) {}

void OmniDevToolsWindowDelegate::OnWindowCreated(CefRefPtr<CefWindow> window) {
  window->SetToFillLayout();
  if (view_) {
    window->AddChildView(view_);
  }
  window->SetTitle("DevTools");
  window->Show();
}

void OmniDevToolsWindowDelegate::OnWindowDestroyed(CefRefPtr<CefWindow> window) {
  (void)window;
  view_ = nullptr;
}

bool OmniDevToolsWindowDelegate::CanClose(CefRefPtr<CefWindow> window) {
  (void)window;
  if (view_) {
    if (auto browser = view_->GetBrowser()) {
      return browser->GetHost()->TryCloseBrowser();
    }
  }
  return true;
}

CefSize OmniDevToolsWindowDelegate::GetPreferredSize(CefRefPtr<CefView> view) {
  (void)view;
  return CefSize(1000, 760);
}

cef_runtime_style_t OmniDevToolsWindowDelegate::GetWindowRuntimeStyle() {
  // DevTools frontend is Chrome WebUI. An Alloy window cannot host it and
  // will take down the process when the inspector loads.
  (void)runtime_style_;
  return CEF_RUNTIME_STYLE_CHROME;
}

bool OmniDevToolsWindowDelegate::IsFrameless(CefRefPtr<CefWindow> window) {
  (void)window;
  return false;
}

bool OmniDevToolsWindowDelegate::CanResize(CefRefPtr<CefWindow> window) {
  (void)window;
  return true;
}

bool OmniDevToolsWindowDelegate::CanMaximize(CefRefPtr<CefWindow> window) {
  (void)window;
  return true;
}

bool OmniDevToolsWindowDelegate::CanMinimize(CefRefPtr<CefWindow> window) {
  (void)window;
  return true;
}

OmniBrowserViewDelegate::OmniBrowserViewDelegate(
    cef_runtime_style_t runtime_style,
    BrowserPane pane)
    : runtime_style_(runtime_style), pane_(pane) {}

void OmniBrowserViewDelegate::OnBrowserCreated(
    CefRefPtr<CefBrowserView> browser_view,
    CefRefPtr<CefBrowser> browser) {
  (void)browser_view;
  if (auto* handler = OmniHandler::GetInstance()) {
    handler->RegisterBrowserPane(browser, pane_);
  }
}

void OmniBrowserViewDelegate::OnBrowserDestroyed(
    CefRefPtr<CefBrowserView> browser_view,
    CefRefPtr<CefBrowser> browser) {
  (void)browser_view;
  if (auto* handler = OmniHandler::GetInstance()) {
    handler->UnregisterBrowserPane(browser);
  }
}

CefRefPtr<CefBrowserViewDelegate>
OmniBrowserViewDelegate::GetDelegateForPopupBrowserView(
    CefRefPtr<CefBrowserView> browser_view,
    const CefBrowserSettings& settings,
    CefRefPtr<CefClient> client,
    bool is_devtools) {
  (void)browser_view;
  (void)settings;
  (void)client;
  if (is_devtools) {
    return new OmniBrowserViewDelegate(CEF_RUNTIME_STYLE_CHROME,
                                       BrowserPane::DevTools);
  }
  return this;
}

bool OmniBrowserViewDelegate::OnPopupBrowserViewCreated(
    CefRefPtr<CefBrowserView> browser_view,
    CefRefPtr<CefBrowserView> popup_browser_view,
    bool is_devtools) {
  (void)browser_view;
  if (!is_devtools) {
    if (popup_browser_view) {
      if (auto browser = popup_browser_view->GetBrowser()) {
        browser->GetHost()->CloseBrowser(true);
      }
    }
    return true;
  }
  if (auto* handler = OmniHandler::GetInstance()) {
    if (popup_browser_view) {
      if (auto browser = popup_browser_view->GetBrowser()) {
        handler->RegisterDevToolsBrowser(browser);
      }
    }
  }
  CefWindow::CreateTopLevelWindow(
      new OmniDevToolsWindowDelegate(popup_browser_view, runtime_style_));
  return true;
}

cef_runtime_style_t OmniBrowserViewDelegate::GetBrowserRuntimeStyle() {
  return runtime_style_;
}

CefSize OmniBrowserViewDelegate::GetPreferredSize(CefRefPtr<CefView> view) {
  (void)view;
  if (pane_ == BrowserPane::DevTools) {
    return CefSize(1000, 760);
  }
  if (pane_ == BrowserPane::Overlay) {
    // Actual bounds are controlled via CefOverlayController.
    return CefSize(244, 280);
  }
  if (pane_ == BrowserPane::Shell) {
    if (ContentIsVisible()) {
      // Browsing: shell is exactly the chrome strip (titlebar + topbar).
      return CefSize(1280, CurrentChromeHeight());
    }
    // Start page: preferred height is nominal; flex=1 stretches to fill.
    return CefSize(1280, 800);
  }
  // Content: nominal preferred size; flex=1 takes the remaining space.
  return CefSize(1280, 600);
}

CefSize OmniBrowserViewDelegate::GetMinimumSize(CefRefPtr<CefView> view) {
  (void)view;
  if (pane_ == BrowserPane::Shell && ContentIsVisible()) {
    return CefSize(200, CurrentChromeHeight());
  }
  return CefSize();
}

}  // namespace omni
