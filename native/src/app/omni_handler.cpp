#include "omni/omni_handler.h"

#include <algorithm>
#include <cstdio>
#include <sstream>
#include <string>
#include <vector>

#include "include/cef_app.h"
#include "include/cef_browser.h"
#include "include/cef_parser.h"
#include "include/base/cef_callback.h"
#include "include/views/cef_box_layout.h"
#include "include/views/cef_browser_view.h"
#include "include/views/cef_layout.h"
#include "include/views/cef_menu_button.h"
#include "include/views/cef_window.h"
#include "include/wrapper/cef_closure_task.h"
#include "include/wrapper/cef_helpers.h"
#include "include/cef_cookie.h"
#include "omni/adblock_resource_handler.h"
#include "omni/agent_api.h"
#include "omni/ai_hud.h"
#include "omni/appearance.h"
#include "omni/log.h"
#include "omni/adblock_service.h"
#include "omni/app_menu_button.h"
#include "omni/dev_mode.h"
#include "omni/download_store.h"
#include "omni/devtools_client.h"
#include "omni/library_ipc.h"
#include "omni/mcp/mcp_server.h"
#include "omni/paths.h"
#include "omni/scrollbar_inject.h"
#include "omni/utf8.h"
#include "omni/window_delegate.h"

#if defined(OS_WIN)
#include <windows.h>
#include <psapi.h>
#include <shellapi.h>
#pragma comment(lib, "psapi.lib")
#endif

#include <chrono>
#include <filesystem>

namespace omni {
namespace {

OmniHandler* g_instance = nullptr;

constexpr size_t kMaxContentTabs = 16;

bool IsDevToolsUrl(const std::string& url) {
  return url.rfind("devtools:", 0) == 0 ||
         url.rfind("chrome-devtools:", 0) == 0;
}

std::string GetDataURI(const std::string& data, const std::string& mime_type) {
  return "data:" + mime_type + ";base64," +
         CefURIEncode(CefBase64Encode(data.data(), data.size()), false)
             .ToString();
}

bool IsAboutUrl(const std::string& url) {
  return url.empty() || url == "about:blank" || url.rfind("about:", 0) == 0;
}

// StopLoad during the first about:blank document crashes CEF Alloy views.
void SafeStopLoad(CefRefPtr<CefBrowser> browser) {
  if (!browser || !browser->IsLoading()) {
    return;
  }
  auto frame = browser->GetMainFrame();
  const std::string url =
      (frame && frame->IsValid()) ? frame->GetURL().ToString() : std::string();
  if (IsAboutUrl(url)) {
    return;
  }
  browser->StopLoad();
}

}  // namespace

OmniHandler::OmniHandler(bool alloy_style) : alloy_style_(alloy_style) {
  DCHECK(!g_instance);
  g_instance = this;

  paths::EnsureAppDataDir();
  if (!repository_.Open(paths::DatabasePath())) {
    LOG(ERROR) << "Failed to open library database";
  }
}

OmniHandler::~OmniHandler() {
  DestroyAiHud();
  hot_reload_.Stop();
  g_instance = nullptr;
}

OmniHandler* OmniHandler::GetInstance() {
  return g_instance;
}

void OmniHandler::SetShellBrowserView(CefRefPtr<CefBrowserView> view) {
  shell_browser_view_ = view;
}

void OmniHandler::SetContentBrowserView(CefRefPtr<CefBrowserView> view) {
  if (view) {
    recycled_content_views_.push_back(view);
  }
}

void OmniHandler::SetOverlayBrowserView(CefRefPtr<CefBrowserView> view) {
  overlay_browser_view_ = view;
}

void OmniHandler::SetOverlayController(
    CefRefPtr<CefOverlayController> controller) {
  overlay_controller_ = controller;
}

CefRefPtr<CefBrowserView> OmniHandler::ActiveContentView() const {
  if (active_tab_id_.empty()) {
    return nullptr;
  }
  return ContentViewForTab(active_tab_id_);
}

CefRefPtr<CefBrowserView> OmniHandler::ContentViewForTab(
    const std::string& tab_id) const {
  if (tab_id.empty()) {
    return nullptr;
  }
  const auto it = tab_content_views_.find(tab_id);
  if (it == tab_content_views_.end()) {
    return nullptr;
  }
  return it->second;
}

std::string OmniHandler::TabIdForContentBrowser(
    CefRefPtr<CefBrowser> browser) const {
  if (!browser) {
    return {};
  }
  auto view = CefBrowserView::GetForBrowser(browser);
  if (!view) {
    return {};
  }
  for (const auto& entry : tab_content_views_) {
    if (entry.second && entry.second->IsSame(view)) {
      return entry.first;
    }
  }
  return {};
}

bool OmniHandler::IsContentBrowser(CefRefPtr<CefBrowser> browser) const {
  if (!browser) {
    return false;
  }
  {
    std::lock_guard<std::mutex> lock(browser_ids_mu_);
    if (devtools_browser_ids_.count(browser->GetIdentifier()) > 0) {
      return false;
    }
    if (content_browser_ids_.count(browser->GetIdentifier()) > 0) {
      return true;
    }
  }
  if (!TabIdForContentBrowser(browser).empty()) {
    return true;
  }
  for (const auto& recycled : recycled_content_views_) {
    if (recycled) {
      if (auto content = recycled->GetBrowser()) {
        if (content->IsSame(browser)) {
          return true;
        }
      }
    }
  }
  return false;
}

bool OmniHandler::ShouldFilterNetwork(CefRefPtr<CefBrowser> browser) const {
  if (!browser) {
    // Service worker / CefURLRequest traffic without a browser: filter it
    // like Brave filters everything that is not app UI.
    return true;
  }
  const int id = browser->GetIdentifier();
  std::lock_guard<std::mutex> lock(browser_ids_mu_);
  if (id == shell_browser_id_ || id == overlay_browser_id_ ||
      devtools_browser_ids_.count(id) > 0) {
    return false;
  }
  // Known content pane, or any unknown browser (popups, recycled views):
  // default to filtering — app chrome is always registered as shell/overlay.
  return true;
}

void OmniHandler::RegisterBrowserPane(CefRefPtr<CefBrowser> browser,
                                      BrowserPane pane) {
  if (!browser) {
    return;
  }
  const int id = browser->GetIdentifier();
  std::lock_guard<std::mutex> lock(browser_ids_mu_);
  switch (pane) {
    case BrowserPane::Shell:
      shell_browser_id_ = id;
      content_browser_ids_.erase(id);
      break;
    case BrowserPane::Overlay:
      overlay_browser_id_ = id;
      content_browser_ids_.erase(id);
      break;
    case BrowserPane::Content:
      if (devtools_browser_ids_.count(id) == 0) {
        content_browser_ids_.insert(id);
      }
      break;
    case BrowserPane::DevTools:
      devtools_browser_ids_.insert(id);
      content_browser_ids_.erase(id);
      break;
  }
}

void OmniHandler::UnregisterBrowserPane(CefRefPtr<CefBrowser> browser) {
  if (!browser) {
    return;
  }
  const int id = browser->GetIdentifier();
  std::lock_guard<std::mutex> lock(browser_ids_mu_);
  content_browser_ids_.erase(id);
  if (shell_browser_id_ == id) {
    shell_browser_id_ = -1;
  }
  if (overlay_browser_id_ == id) {
    overlay_browser_id_ = -1;
  }
  devtools_browser_ids_.erase(id);
}

void OmniHandler::RegisterDevToolsBrowser(CefRefPtr<CefBrowser> browser) {
  if (!browser) {
    return;
  }
  const int id = browser->GetIdentifier();
  std::lock_guard<std::mutex> lock(browser_ids_mu_);
  devtools_browser_ids_.insert(id);
  content_browser_ids_.erase(id);
}

bool OmniHandler::IsDevToolsBrowser(CefRefPtr<CefBrowser> browser) const {
  if (!browser) {
    return false;
  }
  {
    std::lock_guard<std::mutex> lock(browser_ids_mu_);
    if (devtools_browser_ids_.count(browser->GetIdentifier()) > 0) {
      return true;
    }
  }
  if (auto frame = browser->GetMainFrame()) {
    return IsDevToolsUrl(frame->GetURL().ToString());
  }
  return false;
}

void OmniHandler::TrackBrowserIdentity(CefRefPtr<CefBrowser> browser) {
  if (!browser) {
    return;
  }
  const int id = browser->GetIdentifier();
  std::lock_guard<std::mutex> lock(browser_ids_mu_);
  // Prefer view-map classification on the UI thread (caller).
  if (shell_browser_view_) {
    if (auto shell = shell_browser_view_->GetBrowser()) {
      if (shell->IsSame(browser)) {
        shell_browser_id_ = id;
        content_browser_ids_.erase(id);
        return;
      }
    }
  }
  if (overlay_browser_view_) {
    if (auto overlay = overlay_browser_view_->GetBrowser()) {
      if (overlay->IsSame(browser)) {
        overlay_browser_id_ = id;
        content_browser_ids_.erase(id);
        return;
      }
    }
  }
  if (devtools_browser_ids_.count(id) > 0) {
    content_browser_ids_.erase(id);
    return;
  }
  if (!TabIdForContentBrowser(browser).empty()) {
    content_browser_ids_.insert(id);
    return;
  }
  for (const auto& recycled : recycled_content_views_) {
    if (recycled) {
      if (auto content = recycled->GetBrowser()) {
        if (content->IsSame(browser)) {
          content_browser_ids_.insert(id);
          return;
        }
      }
    }
  }
}

void OmniHandler::UntrackBrowserIdentity(CefRefPtr<CefBrowser> browser) {
  if (!browser) {
    return;
  }
  const int id = browser->GetIdentifier();
  content_seed_idle_ids_.erase(id);
  recycling_content_browser_ids_.erase(id);
  std::lock_guard<std::mutex> lock(browser_ids_mu_);
  content_browser_ids_.erase(id);
  if (shell_browser_id_ == id) {
    shell_browser_id_ = -1;
  }
  if (overlay_browser_id_ == id) {
    overlay_browser_id_ = -1;
  }
  devtools_browser_ids_.erase(id);
}

bool OmniHandler::IsShellBrowser(CefRefPtr<CefBrowser> browser) const {
  if (!browser) {
    return false;
  }
  const int id = browser->GetIdentifier();
  {
    std::lock_guard<std::mutex> lock(browser_ids_mu_);
    if (shell_browser_id_ != -1 && id == shell_browser_id_) {
      return true;
    }
  }
  if (!shell_browser_view_) {
    return false;
  }
  auto shell = shell_browser_view_->GetBrowser();
  return shell && shell->IsSame(browser);
}

bool OmniHandler::IsOverlayBrowser(CefRefPtr<CefBrowser> browser) const {
  if (!browser) {
    return false;
  }
  const int id = browser->GetIdentifier();
  {
    std::lock_guard<std::mutex> lock(browser_ids_mu_);
    if (overlay_browser_id_ != -1 && id == overlay_browser_id_) {
      return true;
    }
  }
  if (!overlay_browser_view_) {
    return false;
  }
  auto overlay = overlay_browser_view_->GetBrowser();
  return overlay && overlay->IsSame(browser);
}

CefRefPtr<CefBrowser> OmniHandler::ContentBrowser() const {
  if (auto view = ActiveContentView()) {
    return view->GetBrowser();
  }
  return nullptr;
}

bool OmniHandler::IsContentSeedIdle(CefRefPtr<CefBrowser> browser) const {
  return browser && content_seed_idle_ids_.count(browser->GetIdentifier()) > 0;
}

void OmniHandler::MarkContentSeedIdle(CefRefPtr<CefBrowser> browser) {
  if (browser) {
    content_seed_idle_ids_.insert(browser->GetIdentifier());
  }
}

void OmniHandler::ClearContentSeedIdle(CefRefPtr<CefBrowser> browser) {
  if (browser) {
    content_seed_idle_ids_.erase(browser->GetIdentifier());
  }
}

bool OmniHandler::ContentViewIsBooting(CefRefPtr<CefBrowserView> view) const {
  if (!view) {
    return false;
  }
  auto browser = view->GetBrowser();
  if (!browser) {
    return true;
  }
  return !IsContentSeedIdle(browser);
}

bool OmniHandler::AnyContentViewBooting() const {
  for (const auto& entry : tab_content_views_) {
    if (ContentViewIsBooting(entry.second)) {
      return true;
    }
  }
  for (const auto& view : recycled_content_views_) {
    if (ContentViewIsBooting(view)) {
      return true;
    }
  }
  return false;
}

bool OmniHandler::EnsureContentTab(const std::string& tab_id) {
  CEF_REQUIRE_UI_THREAD();
  if (tab_id.empty()) {
    return false;
  }
  if (ContentViewForTab(tab_id)) {
    return true;
  }
  if (tab_content_views_.size() >= kMaxContentTabs) {
    omni::Log("EnsureContentTab: refusing new tab, max " +
              std::to_string(kMaxContentTabs) + " reached");
    return false;
  }

  CefRefPtr<CefBrowserView> view;
  while (!recycled_content_views_.empty() && !view) {
    view = recycled_content_views_.back();
    recycled_content_views_.pop_back();
    // Keep views whose browser is not attached yet (seed pane created in
    // OnContextInitialized). Dropping them created a second Alloy view and
    // left the seed child in the window tree — Wikipedia restore then CHECKs.
  }
  if (view) {
    if (auto browser = view->GetBrowser()) {
      recycling_content_browser_ids_.insert(browser->GetIdentifier());
      if (auto frame = browser->GetMainFrame()) {
        const std::string current = frame->GetURL().ToString();
        if (!IsAboutUrl(current)) {
          ClearContentSeedIdle(browser);
          SafeStopLoad(browser);
          frame->LoadURL("about:blank");
        }
      }
    }
    omni::Log("EnsureContentTab: reuse view for " + tab_id);
  } else {
    if (AnyContentViewBooting()) {
      omni::Log("EnsureContentTab: defer new Alloy view for " + tab_id +
                " (another content seed still attaching)");
      const std::string tab_copy = tab_id;
      CefPostDelayedTask(
          TID_UI,
          base::BindOnce(
              [](std::string id) {
                if (auto* handler = OmniHandler::GetInstance()) {
                  if (handler->EnsureContentTab(id)) {
                    handler->FlushPendingContentUrl(id);
                  }
                }
              },
              tab_copy),
          250);
      return false;
    }
    if (!shell_browser_view_) {
      return false;
    }
    auto window = shell_browser_view_->GetWindow();
    if (!window) {
      return false;
    }
    const cef_runtime_style_t runtime_style =
        alloy_style_ ? CEF_RUNTIME_STYLE_ALLOY : CEF_RUNTIME_STYLE_CHROME;
    CefBrowserSettings content_settings;
    CefRefPtr<OmniBrowserViewDelegate> content_delegate(
        new OmniBrowserViewDelegate(runtime_style, BrowserPane::Content));
    view = CefBrowserView::CreateBrowserView(this, "about:blank",
                                             content_settings, nullptr,
                                             nullptr, content_delegate);
    if (!view) {
      return false;
    }
    window->AddChildViewAt(view, 1);
    view->SetVisible(false);
    omni::Log("EnsureContentTab: created view for " + tab_id);
  }

  tab_content_views_[tab_id] = view;
  if (auto browser = view->GetBrowser()) {
    TrackBrowserIdentity(browser);
  }
  return true;
}

bool OmniHandler::ActivateContentTab(const std::string& tab_id) {
  CEF_REQUIRE_UI_THREAD();
  active_tab_id_ = tab_id;
  const bool has_view = ContentViewForTab(tab_id) != nullptr;
  content_visible_ = has_view;
  LayoutContentBrowser();
  EmitBrowserEvent(Json{{"type", "visibility"},
                        {"visible", content_visible_},
                        {"tabId", tab_id}});
  return has_view;
}

void OmniHandler::CloseContentTab(const std::string& tab_id) {
  CEF_REQUIRE_UI_THREAD();
  if (tab_id.empty()) {
    return;
  }
  auto it = tab_content_views_.find(tab_id);
  if (it == tab_content_views_.end()) {
    tab_audio_playing_.erase(tab_id);
    tab_media_.erase(tab_id);
    tab_titles_.erase(tab_id);
    pending_content_urls_.erase(tab_id);
    EmitBrowserEvent(Json{{"type", "media"},
                          {"tabId", tab_id},
                          {"active", false},
                          {"playing", false}});
    if (active_tab_id_ == tab_id) {
      active_tab_id_.clear();
      content_visible_ = false;
      LayoutContentBrowser();
    }
    return;
  }

  CefRefPtr<CefBrowserView> view = it->second;
  tab_content_views_.erase(it);
  tab_audio_playing_.erase(tab_id);
  tab_media_.erase(tab_id);
  tab_titles_.erase(tab_id);
  pending_content_urls_.erase(tab_id);
  EmitBrowserEvent(Json{{"type", "media"},
                        {"tabId", tab_id},
                        {"active", false},
                        {"playing", false}});

  if (view) {
    view->SetVisible(false);

    if (auto browser = view->GetBrowser()) {
      recycling_content_browser_ids_.insert(browser->GetIdentifier());
      if (auto frame = browser->GetMainFrame()) {
        const std::string current = frame->GetURL().ToString();
        // Closing a still-booting New Tab must not StopLoad/LoadURL. Session
        // restore does this ~3s after start, then Wikipedia navigates on the
        // sibling view and CEF CHECKs.
        if (!IsAboutUrl(current)) {
          ClearContentSeedIdle(browser);
          SafeStopLoad(browser);
          frame->LoadURL("about:blank");
        }
      }
    }
    omni::Log("CloseContentTab: " + tab_id);
    if (recycled_content_views_.size() < 12) {
      recycled_content_views_.push_back(view);
    } else {
      if (auto browser = view->GetBrowser()) {
        browser->GetHost()->CloseBrowser(false);
      }
    }
  }

  if (active_tab_id_ == tab_id) {
    active_tab_id_.clear();
    content_visible_ = false;
  }
  LayoutContentBrowser();
}

bool OmniHandler::ShouldOpenInContent(const std::string& url) const {
  if (url.empty() || url == "about:blank") {
    return false;
  }
  // Keep shell on local UI / data URIs only.
  if (url.rfind("file:", 0) == 0 || url.rfind("data:", 0) == 0 ||
      url.rfind("chrome:", 0) == 0 || url.rfind("devtools:", 0) == 0) {
    return false;
  }
  return true;
}

bool OmniHandler::OpenInContentBrowser(const std::string& url) {
  if (!ShouldOpenInContent(url)) {
    return false;
  }
  return ContentNavigate(url);
}

bool OmniHandler::ContentNavigate(const std::string& url) {
  return ContentNavigate(url, active_tab_id_);
}

bool OmniHandler::ContentNavigate(const std::string& url,
                                  const std::string& tab_id) {
  CEF_REQUIRE_UI_THREAD();
  std::string id = tab_id;
  if (id.empty()) {
    id = active_tab_id_;
  }
  if (id.empty()) {
    return false;
  }
  if (!EnsureContentTab(id)) {
    if (!shell_browser_view_ || tab_content_views_.size() >= kMaxContentTabs) {
      return false;
    }
    pending_content_urls_[id] = url;
    omni::Log("ContentNavigate: waiting for view " + url + " tab=" + id);
    return true;
  }
  if (active_tab_id_ != id) {
    active_tab_id_ = id;
  }

  auto view = ContentViewForTab(id);
  if (!view) {
    return false;
  }
  auto browser = view->GetBrowser();
  CefRefPtr<CefFrame> frame = browser ? browser->GetMainFrame() : nullptr;

  // Seed content views start on about:blank. LoadURL of Wikipedia (or any
  // real page) before that first document finishes CHECKs CEF Alloy.
  const bool booting = !browser || !frame || !frame->IsValid() ||
                       !IsContentSeedIdle(browser);
  if (booting) {
    const bool already_queued = pending_content_urls_.count(id) > 0;
    pending_content_urls_[id] = url;
    active_tab_id_ = id;
    content_visible_ = true;
    LayoutContentBrowser();
    omni::Log("ContentNavigate: queued " + url + " for " + id +
              (browser ? " (seed not idle)" : " (no browser)"));
    if (!already_queued) {
      const std::string tab_copy = id;
      CefPostDelayedTask(
          TID_UI,
          base::BindOnce(
              [](std::string tab_id) {
                if (auto* handler = OmniHandler::GetInstance()) {
                  if (auto view = handler->ContentViewForTab(tab_id)) {
                    handler->MarkContentSeedIdle(view->GetBrowser());
                  }
                  handler->FlushPendingContentUrl(tab_id);
                }
              },
              tab_copy),
          5000);
    }
    return true;
  }

  pending_content_urls_.erase(id);

  omni::Log("ContentNavigate: " + url + " tab=" + id);
  // Cancel in-flight loads only after the seed document is gone.
  SafeStopLoad(browser);
  // Load before revealing the pane so the previous page is never flashed.
  frame->LoadURL(url);

  content_visible_ = true;
  LayoutContentBrowser();

  EmitContentEvent(browser,
                   Json{{"type", "navigate"},
                        {"url", url},
                        {"visible", true},
                        {"loading", true},
                        {"canGoBack", browser->CanGoBack()},
                        {"canGoForward", browser->CanGoForward()}});

  CefPostDelayedTask(TID_UI, base::BindOnce([]() {
                       if (auto* handler = OmniHandler::GetInstance()) {
                         handler->LayoutContentBrowser();
                       }
                     }),
                     48);
  return true;
}

void OmniHandler::FlushPendingContentUrl(const std::string& tab_id) {
  CEF_REQUIRE_UI_THREAD();
  if (tab_id.empty()) {
    return;
  }
  const auto pending = pending_content_urls_.find(tab_id);
  if (pending == pending_content_urls_.end()) {
    return;
  }
  const std::string url = pending->second;
  if (url.empty() || url == "about:blank") {
    pending_content_urls_.erase(pending);
    return;
  }
  ContentNavigate(url, tab_id);
}

void OmniHandler::ContentGoBack() {
  CEF_REQUIRE_UI_THREAD();
  auto browser = ContentBrowser();
  if (!browser || !browser->CanGoBack()) {
    return;
  }
  SafeStopLoad(browser);
  browser->GoBack();
}

void OmniHandler::ContentGoForward() {
  CEF_REQUIRE_UI_THREAD();
  auto browser = ContentBrowser();
  if (!browser || !browser->CanGoForward()) {
    return;
  }
  SafeStopLoad(browser);
  browser->GoForward();
}

void OmniHandler::ContentReload(bool ignore_cache) {
  CEF_REQUIRE_UI_THREAD();
  auto browser = ContentBrowser();
  if (!browser) {
    return;
  }
  if (ignore_cache) {
    browser->ReloadIgnoreCache();
  } else {
    browser->Reload();
  }
}

bool OmniHandler::OnPreKeyEvent(CefRefPtr<CefBrowser> browser,
                                const CefKeyEvent& event,
                                CefEventHandle os_event,
                                bool* is_keyboard_shortcut) {
  CEF_REQUIRE_UI_THREAD();
  (void)os_event;
  (void)is_keyboard_shortcut;

  if (!browser) {
    return false;
  }
  if (event.type != KEYEVENT_RAWKEYDOWN && event.type != KEYEVENT_KEYDOWN) {
    return false;
  }

  const bool ctrl = (event.modifiers & EVENTFLAG_CONTROL_DOWN) != 0;
  const bool shift = (event.modifiers & EVENTFLAG_SHIFT_DOWN) != 0;
  const bool alt = (event.modifiers & EVENTFLAG_ALT_DOWN) != 0;
  const int key = event.windows_key_code;

  if (!IsDevToolsBrowser(browser) &&
      (key == VK_F12 ||
       (ctrl && shift && !alt && (key == 'I' || key == 'i')))) {
    ToggleDevTools();
    return true;
  }

  if (!IsContentBrowser(browser) || !content_visible_) {
    return false;
  }

  bool hard = false;
  bool reload = false;
  if (key == VK_F5) {
    reload = true;
    hard = shift || ctrl;
  } else if (ctrl && (key == 'R' || key == 'r')) {
    reload = true;
    hard = shift;
  }

  if (!reload) {
    return false;
  }
  ContentReload(hard);
  return true;
}

void OmniHandler::ToggleDevTools() {
  CEF_REQUIRE_UI_THREAD();
  // MenuButton::ExecuteCommand runs while the native menu is still tearing
  // down. Creating a top-level window in that stack can close the host.
  CefPostTask(TID_UI, base::BindOnce([]() {
                if (auto* self = OmniHandler::GetInstance()) {
                  self->ShowDevToolsNow();
                }
              }));
}

void OmniHandler::ShowDevToolsNow() {
  CEF_REQUIRE_UI_THREAD();
  CefRefPtr<CefBrowser> browser;
  if (content_visible_) {
    browser = ContentBrowser();
  }
  if (!browser && shell_browser_view_) {
    browser = shell_browser_view_->GetBrowser();
  }
  if (!browser) {
    return;
  }
  auto host = browser->GetHost();
  if (!host) {
    return;
  }
  if (host->HasDevTools()) {
    host->CloseDevTools();
    return;
  }
  CefWindowInfo window_info;
#if defined(OS_WIN)
  window_info.SetAsPopup(nullptr, "DevTools");
#endif
  window_info.runtime_style = CEF_RUNTIME_STYLE_CHROME;
  CefBrowserSettings settings;
  host->ShowDevTools(window_info, this, settings, CefPoint());
}

void OmniHandler::ContentStop() {
  CEF_REQUIRE_UI_THREAD();
  auto browser = ContentBrowser();
  if (browser) {
    SafeStopLoad(browser);
  }
}

void OmniHandler::ContentClear() {
  CEF_REQUIRE_UI_THREAD();
  auto browser = ContentBrowser();
  if (!browser) {
    return;
  }
  SafeStopLoad(browser);
  ClearContentSeedIdle(browser);
  // Only wipe the active tab document; background tabs keep their media.
  if (auto frame = browser->GetMainFrame()) {
    frame->LoadURL("about:blank");
  }
}

void OmniHandler::SetContentVisible(bool visible) {
  CEF_REQUIRE_UI_THREAD();
  content_visible_ = visible && ActiveContentView() != nullptr;
  LayoutContentBrowser();
  EmitBrowserEvent(Json{{"type", "visibility"},
                        {"visible", content_visible_},
                        {"tabId", active_tab_id_}});
}

void OmniHandler::SetChromeHeight(int height) {
  CEF_REQUIRE_UI_THREAD();
  if (height < 40) {
    height = 40;
  }
  if (height == chrome_height_) {
    return;
  }
  chrome_height_ = height;
  LayoutContentBrowser();
}

void OmniHandler::LayoutContentBrowser() {
  CEF_REQUIRE_UI_THREAD();
  if (!shell_browser_view_) {
    return;
  }
  auto window = shell_browser_view_->GetWindow();
  if (!window) {
    return;
  }

  CefRefPtr<CefBoxLayout> box;
  if (auto layout = window->GetLayout()) {
    box = layout->AsBoxLayout();
  }

  const bool show_content = content_visible_ && !active_tab_id_.empty() &&
                            ContentViewForTab(active_tab_id_) != nullptr;

  for (const auto& recycled : recycled_content_views_) {
    if (recycled) {
      recycled->SetVisible(false);
      if (box) {
        box->SetFlexForView(recycled, 0);
      }
    }
  }

  for (const auto& entry : tab_content_views_) {
    const bool show = show_content && entry.first == active_tab_id_;
    if (entry.second) {
      entry.second->SetVisible(show);
      if (box) {
        box->SetFlexForView(entry.second, show ? 1 : 0);
      }
      entry.second->InvalidateLayout();
    }
  }

  if (box) {
    if (show_content) {
      box->SetFlexForView(shell_browser_view_, 0);
    } else {
      box->SetFlexForView(shell_browser_view_, 1);
    }
  }

  shell_browser_view_->InvalidateLayout();
  window->InvalidateLayout();
  LayoutAiHud();
}

void OmniHandler::ApplyOverlayBounds(int height) {
  if (!overlay_controller_) {
    return;
  }

  int win_w = 0;
  int win_h = 0;
  if (shell_browser_view_) {
    if (auto window = shell_browser_view_->GetWindow()) {
      const CefRect bounds = window->GetBounds();
      win_w = bounds.width;
      win_h = bounds.height;
    }
  }

  int h = height;
  if (win_h > 40) {
    const int max_height = win_h - overlay_anchor_top_ - 8;
    if (max_height > 40 && h > max_height) {
      h = max_height;
    }
  }
  if (h < 40) {
    h = 40;
  }

  int x = overlay_anchor_right_ - overlay_width_;
  int y = overlay_anchor_top_;
  constexpr int kPad = 8;
  if (x < kPad) {
    x = kPad;
  }
  if (win_w > 0 && x + overlay_width_ > win_w - kPad) {
    x = win_w - overlay_width_ - kPad;
    if (x < kPad) {
      x = kPad;
    }
  }
  if (win_h > 0 && y + h > win_h - kPad) {
    y = win_h - h - kPad;
    if (y < 0) {
      y = 0;
    }
  }

  overlay_height_ = h;
  overlay_controller_->SetBounds(CefRect(x, y, overlay_width_, h));
}

void OmniHandler::EnsureAppMenuButton() {
  if (app_menu_button_ || !shell_browser_view_) {
    return;
  }
  auto window = shell_browser_view_->GetWindow();
  if (!window) {
    return;
  }

  app_menu_button_host_ = new AppMenuButtonHost();
  app_menu_button_ =
      CefMenuButton::CreateMenuButton(app_menu_button_host_, "");
  if (!app_menu_button_) {
    app_menu_button_host_ = nullptr;
    return;
  }
  app_menu_button_->SetFocusable(false);

  // Invisible 1x1 host — only needed so ShowMenu uses the MenuButton path
  // (HAS_MNEMONICS) like Chrome/Brave, not Window::ShowMenu (CONTEXT_MENU).
  app_menu_button_controller_ = window->AddOverlayView(
      app_menu_button_, CEF_DOCKING_MODE_CUSTOM, /*can_activate=*/false);
  if (app_menu_button_controller_) {
    app_menu_button_controller_->SetBounds(CefRect(0, 0, 1, 1));
    app_menu_button_controller_->SetVisible(false);
  }
}

void OmniHandler::ShowAppMenu(int anchor_left,
                              int anchor_top,
                              int anchor_right,
                              int anchor_bottom,
                              const Json& payload) {
  CEF_REQUIRE_UI_THREAD();
  if (!shell_browser_view_) {
    return;
  }
  if (!shell_browser_view_->GetWindow()) {
    return;
  }

  // Cancel any previous menu so CEF never stays in a stale menu-running state
  if (auto window = shell_browser_view_->GetWindow()) {
    window->CancelMenu();
  }
  OverlayHide();
  app_menu_open_ = false;
  app_menu_ = nullptr;

  app_menu_ = new AppMenuDelegate(this, payload);
  CefRefPtr<CefMenuModel> model = app_menu_->Build();
  if (!model) {
    app_menu_ = nullptr;
    return;
  }

  EnsureAppMenuButton();
  if (!app_menu_button_) {
    app_menu_ = nullptr;
    return;
  }

  // Brave/Chrome AppMenu::RunMenu:
  //   RunMenuAt(widget, host, button->GetAnchorBoundsInScreen(), kTopRight)
  // CEF only accepts a point; bottom-right of the button + TOPRIGHT is the
  // same math (menu.right = button.right, menu.top = button.bottom).
  const int left = anchor_left;
  const int top = anchor_top;
  const int right = anchor_right > left ? anchor_right : left;
  const int bottom = anchor_bottom > top ? anchor_bottom : top;

  app_menu_btn_right_ = right;
  app_menu_btn_bottom_ = bottom;

  if (app_menu_button_controller_) {
    app_menu_button_controller_->SetBounds(CefRect(0, 0, 1, 1));
    app_menu_button_controller_->SetVisible(true);
  }

  CefPoint screen_point(right, bottom);
  if (!shell_browser_view_->ConvertPointToScreen(screen_point)) {
    const CefRect shell = shell_browser_view_->GetBoundsInScreen();
    screen_point.x = shell.x + right;
    screen_point.y = shell.y + bottom;
  }

  app_menu_open_ = true;
#if defined(_WIN32)
  ApplyImmersiveMenuAppearance();
  HWND main_hwnd = nullptr;
  if (shell_browser_view_) {
    if (auto window = shell_browser_view_->GetWindow()) {
      main_hwnd = window->GetWindowHandle();
    }
  }
  InstallAppMenuSubmenuHook(main_hwnd);
#endif
  // Must go through MenuButton::ShowMenu (not Window::ShowMenu) so CEF uses
  // HAS_MNEMONICS instead of CONTEXT_MENU — otherwise Windows flips the menu
  // open to the right past the window edge.
  app_menu_button_->ShowMenu(model, screen_point, CEF_MENU_ANCHOR_TOPRIGHT);
}

void OmniHandler::CancelAppMenu() {
  CEF_REQUIRE_UI_THREAD();
  if (!app_menu_open_ || !shell_browser_view_) {
    return;
  }
  if (auto window = shell_browser_view_->GetWindow()) {
    window->CancelMenu();
  }
}

void OmniHandler::OnAppMenuClosed() {
  CEF_REQUIRE_UI_THREAD();
  app_menu_open_ = false;
  app_menu_ = nullptr;
#if defined(_WIN32)
  RemoveAppMenuSubmenuHook();
#endif
  if (app_menu_button_controller_) {
    app_menu_button_controller_->SetBounds(CefRect(0, 0, 1, 1));
    app_menu_button_controller_->SetVisible(false);
  }
  EmitBrowserEvent(Json{{"type", "menu"}, {"visible", false}});
}

void OmniHandler::EmitMenuCommand(const Json& command) {
  CEF_REQUIRE_UI_THREAD();
  EmitBrowserEvent(Json{{"type", "menu-command"}, {"command", command}});
}

namespace {

int64_t NowMs() {
  return static_cast<int64_t>(
      std::chrono::duration_cast<std::chrono::milliseconds>(
          std::chrono::system_clock::now().time_since_epoch())
          .count());
}

std::string DownloadState(CefRefPtr<CefDownloadItem> item) {
  if (!item || !item->IsValid()) {
    return "interrupted";
  }
  if (item->IsComplete()) {
    return "complete";
  }
  if (item->IsCanceled()) {
    return "canceled";
  }
  if (item->IsInterrupted()) {
    return "interrupted";
  }
  if (item->IsInProgress()) {
    return "in_progress";
  }
  return "interrupted";
}

Json DownloadEntryFromItem(CefRefPtr<CefDownloadItem> item,
                           const std::string& suggested_name) {
  Json entry = Json::object();
  if (!item || !item->IsValid()) {
    return entry;
  }
  const std::string id = std::to_string(item->GetId());
  std::string path = item->GetFullPath().ToString();
  std::string filename = suggested_name;
  if (filename.empty()) {
    filename = item->GetSuggestedFileName().ToString();
  }
  if (filename.empty() && !path.empty()) {
    filename = utf8::Narrow(
        std::filesystem::path(utf8::Widen(path)).filename().wstring());
  }
  if (filename.empty()) {
    filename = "download";
  }
  entry = Json{{"id", id},
               {"url", item->GetURL().ToString()},
               {"originalUrl", item->GetOriginalUrl().ToString()},
               {"path", path},
               {"filename", filename},
               {"mime", item->GetMimeType().ToString()},
               {"state", DownloadState(item)},
               {"receivedBytes", item->GetReceivedBytes()},
               {"totalBytes", item->GetTotalBytes()},
               {"percent", item->GetPercentComplete()},
               {"ts", NowMs()}};
  return entry;
}

}  // namespace

bool OmniHandler::OnBeforeDownload(
    CefRefPtr<CefBrowser> browser,
    CefRefPtr<CefDownloadItem> download_item,
    const CefString& suggested_name,
    CefRefPtr<CefBeforeDownloadCallback> callback) {
  CEF_REQUIRE_UI_THREAD();
  (void)browser;
  if (!callback) {
    return false;
  }

  std::string name = suggested_name.ToString();
  if (name.empty() && download_item && download_item->IsValid()) {
    name = download_item->GetSuggestedFileName().ToString();
  }
  if (name.empty()) {
    name = "download";
  }

  // Sanitize path separators in the suggested filename.
  for (char& c : name) {
    if (c == '/' || c == '\\' || c == ':' || c == '*' || c == '?' || c == '"' ||
        c == '<' || c == '>' || c == '|') {
      c = '_';
    }
  }

  const std::string dir = paths::EnsureUserDownloadsDir();
  const std::string full =
      utf8::Narrow((std::filesystem::path(utf8::Widen(dir)) /
                    std::filesystem::path(utf8::Widen(name)))
                       .wstring());

  callback->Continue(full, false);

  Json entry = DownloadEntryFromItem(download_item, name);
  if (!entry.empty()) {
    entry["path"] = full;
    entry["filename"] = name;
    entry["state"] = "in_progress";
    if (entry.value("ts", 0LL) <= 0) {
      entry["ts"] = NowMs();
    }
    downloads::Upsert(entry);
    EmitDownloadProgress();
  }
  return true;
}

void OmniHandler::OnDownloadUpdated(
    CefRefPtr<CefBrowser> browser,
    CefRefPtr<CefDownloadItem> download_item,
    CefRefPtr<CefDownloadItemCallback> callback) {
  CEF_REQUIRE_UI_THREAD();
  (void)browser;
  (void)callback;
  if (!download_item || !download_item->IsValid()) {
    return;
  }
  Json entry = DownloadEntryFromItem(download_item, "");
  if (entry.empty()) {
    return;
  }
  // Preserve the original start timestamp when updating progress.
  const Json existing = downloads::List();
  const std::string id = entry.value("id", std::string());
  for (const auto& prev : existing) {
    if (prev.value("id", std::string()) == id) {
      const int64_t ts = prev.value("ts", 0LL);
      if (ts > 0) {
        entry["ts"] = ts;
      }
      break;
    }
  }
  downloads::Upsert(entry);
  EmitDownloadProgress();
}

void OmniHandler::EmitDownloadProgress() {
  CEF_REQUIRE_UI_THREAD();
  const Json list = downloads::List();
  int active = 0;
  int64_t received = 0;
  int64_t total = 0;
  int percent_sum = 0;
  int percent_n = 0;
  for (const auto& entry : list) {
    if (!entry.is_object()) {
      continue;
    }
    if (entry.value("state", std::string()) != "in_progress") {
      continue;
    }
    active += 1;
    received += entry.value("receivedBytes", 0LL);
    total += entry.value("totalBytes", 0LL);
    const int pct = entry.value("percent", -1);
    if (pct >= 0) {
      percent_sum += pct;
      percent_n += 1;
    }
  }
  int percent = 0;
  if (percent_n > 0) {
    percent = percent_sum / percent_n;
  } else if (total > 0) {
    percent = static_cast<int>((received * 100) / total);
  }
  if (percent < 0) {
    percent = 0;
  }
  if (percent > 100) {
    percent = 100;
  }
  EmitBrowserEvent(Json{{"type", "download"},
                        {"active", active},
                        {"percent", percent},
                        {"receivedBytes", received},
                        {"totalBytes", total}});
}

void OmniHandler::ShowHistoryFlyout(const Json& recent_tabs) {
  CEF_REQUIRE_UI_THREAD();
  // Main menu is TOPRIGHT under the button. Place the favicon flyout at the
  // menu's left edge, aligned with the History row (Brave cascade position).
  constexpr int kMainMenuW = 248;
  constexpr int kRow = 30;
  constexpr int kSep = 9;
  constexpr int kPadY = 4;
  const int flyout_right =
      std::max(8, app_menu_btn_right_ - kMainMenuW);
  const int flyout_top = app_menu_btn_bottom_ + kPadY + 3 * kRow + kSep;

  Json items = Json::array();
  items.push_back(Json{{"type", "action"},
                       {"title", "Open history page"},
                       {"action", "open-history"},
                       {"shortcut", "Ctrl+H"}});
  if (recent_tabs.is_array() && !recent_tabs.empty()) {
    items.push_back(Json{{"type", "separator"}});
    for (const auto& entry : recent_tabs) {
      items.push_back(entry);
    }
  }

  history_flyout_visible_ = true;
  OverlayShow(flyout_right, flyout_top, 300, 0,
              Json{{"view", "history"}, {"items", std::move(items)}});
}

void OmniHandler::HideHistoryFlyout() {
  CEF_REQUIRE_UI_THREAD();
  if (!history_flyout_visible_) {
    return;
  }
  history_flyout_visible_ = false;
  OverlayHide();
}

void OmniHandler::HandleOverlayCommand(const Json& command) {
  CEF_REQUIRE_UI_THREAD();
  history_flyout_visible_ = false;
  CancelAppMenu();
  OverlayHide();
  EmitMenuCommand(command);
}

void OmniHandler::ForceCloseAuxiliaryBrowsers() {
  std::vector<CefRefPtr<CefBrowserView>> views;
  views.reserve(tab_content_views_.size() + 1);
  for (const auto& entry : tab_content_views_) {
    if (entry.second) {
      views.push_back(entry.second);
    }
  }
  for (const auto& recycled : recycled_content_views_) {
    if (recycled) {
      views.push_back(recycled);
    }
  }
  recycled_content_views_.clear();
  active_tab_id_.clear();
  content_visible_ = false;
  tab_audio_playing_.clear();
  tab_media_.clear();
  tab_titles_.clear();
  pending_content_urls_.clear();
  content_seed_idle_ids_.clear();
  recycling_content_browser_ids_.clear();

  if (shell_browser_view_) {
    if (auto shell = shell_browser_view_->GetBrowser()) {
      if (auto host = shell->GetHost()) {
        if (host->HasDevTools()) {
          host->CloseDevTools();
        }
      }
    }
  }
  for (const auto& view : views) {
    if (auto content = view->GetBrowser()) {
      if (auto host = content->GetHost()) {
        if (host->HasDevTools()) {
          host->CloseDevTools();
        }
        host->CloseBrowser(true);
      }
    }
  }
  if (overlay_browser_view_) {
    if (auto overlay = overlay_browser_view_->GetBrowser()) {
      overlay->GetHost()->CloseBrowser(true);
    }
  }

  std::vector<CefRefPtr<CefBrowser>> extras;
  extras.reserve(browsers_.size());
  for (const auto& extra : browsers_) {
    if (extra && !IsShellBrowser(extra) && !IsContentBrowser(extra) &&
        !IsOverlayBrowser(extra)) {
      extras.push_back(extra);
    }
  }
  for (const auto& extra : extras) {
    if (auto host = extra->GetHost()) {
      host->CloseBrowser(true);
    }
  }
}

void OmniHandler::BeginShutdown() {
  CEF_REQUIRE_UI_THREAD();
  if (shutting_down_) {
    return;
  }
  shutting_down_ = true;
  closing_ = true;

  DestroyAiHud();

  McpServer::Get().Shutdown();
  FailPendingAgentQueries("Browser is shutting down");

  CancelAppMenu();
  OverlayHide();
  ForceCloseAuxiliaryBrowsers();

  // If a child browser stalls OnBeforeClose, still leave the message loop so
  // CefShutdown can tear down GPU/renderer processes.
  CefPostDelayedTask(TID_UI, base::BindOnce([]() {
                       if (auto* self = OmniHandler::GetInstance()) {
                         self->FinishShutdown();
                       }
                     }),
                     250);
}

void OmniHandler::FinishShutdown() {
  CEF_REQUIRE_UI_THREAD();
  if (quit_posted_) {
    return;
  }
  quit_posted_ = true;

  ForceCloseAuxiliaryBrowsers();
  hot_reload_.Stop();
  terminals_.CloseAll();
  browser_subscribers_.clear();
  overlay_subscribers_.clear();
  if (message_router_ && library_handler_) {
    message_router_->RemoveHandler(library_handler_);
    delete library_handler_;
    library_handler_ = nullptr;
  }
  message_router_ = nullptr;
  app_menu_button_controller_ = nullptr;
  app_menu_button_ = nullptr;
  app_menu_button_host_ = nullptr;

  if (auto cookies = CefCookieManager::GetGlobalManager(nullptr)) {
    cookies->FlushStore(nullptr);
  }

  CefQuitMessageLoop();
}

void OmniHandler::OverlayShow(int anchor_right,
                              int anchor_top,
                              int width,
                              int height,
                              const Json& payload) {
  CEF_REQUIRE_UI_THREAD();
  if (!overlay_controller_) {
    return;
  }
  // History flyout rides next to the open native menu — do not dismiss it.
  const bool keep_menu =
      payload.is_object() && payload.value("view", "") == "history";
  if (!keep_menu) {
    CancelAppMenu();
  }
  overlay_anchor_right_ = anchor_right;
  overlay_anchor_top_ = anchor_top;
  if (width > 0) {
    overlay_width_ = width;
  }
  if (!overlay_visible_) {
    ApplyOverlayBounds(height > 0 ? height : 120);
    overlay_controller_->SetVisible(true);
    overlay_visible_ = true;
  } else if (height > 0) {
    // Explicit size (menu open / tip first layout) — apply it.
    ApplyOverlayBounds(height);
  } else {
    // height<=0: content-only refresh (tip memory poll). Keep size so the
    // overlay doesn't pulse between the placeholder and measured height.
    ApplyOverlayBounds(overlay_height_ > 0 ? overlay_height_ : 120);
  }
#if defined(OS_WIN)
  if (overlay_browser_view_) {
    if (auto browser = overlay_browser_view_->GetBrowser()) {
      if (auto host = browser->GetHost()) {
        HWND hwnd = host->GetWindowHandle();
        if (hwnd) {
          ::BringWindowToTop(hwnd);
          ::SetWindowPos(hwnd, HWND_TOP, 0, 0, 0, 0,
                         SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_SHOWWINDOW);
        }
      }
    }
  }
#endif
  EmitOverlayEvent(Json{{"type", "show"}, {"payload", payload}});
}

void OmniHandler::OverlayResize(int width, int height) {
  CEF_REQUIRE_UI_THREAD();
  if (!overlay_controller_ || !overlay_visible_) {
    return;
  }
  if (width > 0) {
    overlay_width_ = width;
  }
  ApplyOverlayBounds(height);
}

void OmniHandler::OverlayHide() {
  CEF_REQUIRE_UI_THREAD();
  if (!overlay_visible_) {
    return;
  }
  overlay_visible_ = false;
  if (overlay_controller_) {
    overlay_controller_->SetVisible(false);
  }
  EmitOverlayEvent(Json{{"type", "hide"}});
  EmitBrowserEvent(Json{{"type", "overlay"}, {"visible", false}});
}

void OmniHandler::SetAiActive(bool active, int agent_count) {
  CEF_REQUIRE_UI_THREAD();
  ai_active_ = active;
  ai_agent_count_ = active ? std::max(1, agent_count) : 0;
  EmitBrowserEvent(Json{
      {"type", "ai.active"},
      {"active", active},
      {"agentCount", ai_agent_count_},
  });
  LayoutAiHud();
  if (active) {
    CefPostDelayedTask(TID_UI, base::BindOnce([]() {
                         if (auto* handler = OmniHandler::GetInstance()) {
                           handler->LayoutAiHud();
                         }
                       }),
                       64);
  }
}

void OmniHandler::LayoutAiHud() {
  CEF_REQUIRE_UI_THREAD();
#if defined(OS_WIN)
  HWND parent = nullptr;
  if (shell_browser_view_) {
    if (auto window = shell_browser_view_->GetWindow()) {
      parent = window->GetWindowHandle();
    }
  }
  if (!parent) {
    if (ai_hud_) {
      ai_hud_->Detach();
    }
    return;
  }
  if (!ai_hud_) {
    ai_hud_ = std::make_unique<AiHudOverlay>();
  }
  ai_hud_->Attach(parent);
  ai_hud_->SetActive(ai_active_, ai_agent_count_);
  ai_hud_->Layout(chrome_height_);
#endif
}

void OmniHandler::DestroyAiHud() {
  if (ai_hud_) {
    ai_hud_->Detach();
    ai_hud_.reset();
  }
}

void OmniHandler::MoveAgentPointer(float css_x, float css_y, bool click) {
  CEF_REQUIRE_UI_THREAD();
  LayoutAiHud();
  float scale = 1.0f;
#if defined(OS_WIN)
  if (shell_browser_view_) {
    if (auto window = shell_browser_view_->GetWindow()) {
      HWND hwnd = window->GetWindowHandle();
      if (hwnd) {
        UINT dpi = GetDpiForWindow(hwnd);
        if (dpi > 0) {
          scale = static_cast<float>(dpi) / 96.0f;
        }
      }
    }
  }
#endif
  if (ai_hud_) {
    ai_hud_->MovePointer(css_x * scale, css_y * scale, click);
  }
}

void OmniHandler::SendAgentMouseClick(int css_x, int css_y) {
  CEF_REQUIRE_UI_THREAD();
  auto view = ActiveContentView();
  if (!view) {
    return;
  }
  auto browser = view->GetBrowser();
  if (!browser) {
    return;
  }
  auto host = browser->GetHost();
  if (!host) {
    return;
  }
  CefMouseEvent ev;
  ev.x = css_x;
  ev.y = css_y;
  host->SendMouseMoveEvent(ev, false);
  host->SendMouseClickEvent(ev, MBT_LEFT, false, 1);
  host->SendMouseClickEvent(ev, MBT_LEFT, true, 1);
}

void OmniHandler::SubscribeOverlayEvents(
    int64_t query_id,
    CefRefPtr<CefMessageRouterBrowserSide::Callback> callback) {
  overlay_subscribers_[query_id] = callback;
  callback->Success(Json{{"type", "ready"}}.dump());
}

void OmniHandler::UnsubscribeOverlayEvents(int64_t query_id) {
  overlay_subscribers_.erase(query_id);
}

void OmniHandler::EmitOverlayEvent(const Json& event) {
  const std::string payload = event.dump();
  for (auto& entry : overlay_subscribers_) {
    if (entry.second) {
      entry.second->Success(payload);
    }
  }
}

void OmniHandler::OnGotFocus(CefRefPtr<CefBrowser> browser) {
  CEF_REQUIRE_UI_THREAD();
  // Clicking into the page or the shell chrome dismisses an open tip overlay.
  if (overlay_visible_ &&
      (IsContentBrowser(browser) || IsShellBrowser(browser))) {
    OverlayHide();
  }
}

Json OmniHandler::ContentStateJson() const {
  auto browser = ContentBrowser();
  std::string url;
  std::string title;
  if (!active_tab_id_.empty()) {
    const auto title_it = tab_titles_.find(active_tab_id_);
    if (title_it != tab_titles_.end()) {
      title = title_it->second;
    }
  }
  bool can_back = false;
  bool can_forward = false;
  bool loading = false;
  if (browser) {
    if (auto frame = browser->GetMainFrame()) {
      url = frame->GetURL().ToString();
    }
    can_back = browser->CanGoBack();
    can_forward = browser->CanGoForward();
    loading = browser->IsLoading();
  }
  return Json{{"url", url},
              {"title", title},
              {"canGoBack", can_back},
              {"canGoForward", can_forward},
              {"loading", loading},
              {"visible", content_visible_},
              {"chromeHeight", chrome_height_},
              {"memoryMb", ContentMemoryMb()},
              {"audioPlaying", content_audio_playing()},
              {"audioMuted", ContentAudioMuted()},
              {"tabId", active_tab_id_}};
}

Json OmniHandler::GetTabsListJson() const {
  Json tabs = Json::array();
  for (const auto& entry : tab_content_views_) {
    const std::string& tab_id = entry.first;
    auto view = entry.second;
    std::string url;
    std::string title;
    bool loading = false;
    const auto title_it = tab_titles_.find(tab_id);
    if (title_it != tab_titles_.end()) {
      title = title_it->second;
    }
    if (view) {
      if (auto browser = view->GetBrowser()) {
        if (auto frame = browser->GetMainFrame()) {
          url = frame->GetURL().ToString();
        }
        loading = browser->IsLoading();
      }
    }
    tabs.push_back(Json{
        {"tabId", tab_id},
        {"active", (tab_id == active_tab_id_)},
        {"url", url},
        {"title", title},
        {"loading", loading},
    });
  }
  return tabs;
}

bool OmniHandler::content_audio_playing() const {
  if (active_tab_id_.empty()) {
    return false;
  }
  const auto it = tab_audio_playing_.find(active_tab_id_);
  return it != tab_audio_playing_.end() && it->second;
}

void OmniHandler::SetContentAudioPlaying(CefRefPtr<CefBrowser> browser,
                                         bool playing) {
  CEF_REQUIRE_UI_THREAD();
  std::string tab_id = TabIdForContentBrowser(browser);
  if (tab_id.empty()) {
    tab_id = active_tab_id_;
  }
  if (tab_id.empty()) {
    return;
  }
  const auto it = tab_audio_playing_.find(tab_id);
  const bool prev = it != tab_audio_playing_.end() && it->second;
  if (prev == playing) {
    return;
  }
  tab_audio_playing_[tab_id] = playing;
  EmitBrowserEvent(Json{{"type", "audio"},
                        {"tabId", tab_id},
                        {"playing", playing},
                        {"muted", ContentAudioMuted(tab_id)}});
}

void OmniHandler::SetContentAudioMuted(bool muted) {
  SetContentAudioMuted(active_tab_id_, muted);
}

void OmniHandler::SetContentAudioMuted(const std::string& tab_id, bool muted) {
  CEF_REQUIRE_UI_THREAD();
  if (tab_id.empty()) {
    return;
  }
  auto view = ContentViewForTab(tab_id);
  if (!view) {
    return;
  }
  auto browser = view->GetBrowser();
  if (!browser) {
    return;
  }
  auto host = browser->GetHost();
  if (!host) {
    return;
  }
  if (host->IsAudioMuted() == muted) {
    return;
  }
  host->SetAudioMuted(muted);
  const auto it = tab_audio_playing_.find(tab_id);
  const bool playing = it != tab_audio_playing_.end() && it->second;
  EmitBrowserEvent(Json{{"type", "audio"},
                        {"tabId", tab_id},
                        {"playing", playing},
                        {"muted", muted}});
}

bool OmniHandler::ContentAudioMuted() const {
  return ContentAudioMuted(active_tab_id_);
}

bool OmniHandler::ContentAudioMuted(const std::string& tab_id) const {
  auto view = ContentViewForTab(tab_id);
  if (!view) {
    return false;
  }
  auto browser = view->GetBrowser();
  if (!browser) {
    return false;
  }
  auto host = browser->GetHost();
  return host && host->IsAudioMuted();
}

void OmniHandler::SetContentMedia(CefRefPtr<CefBrowser> browser,
                                  const Json& media) {
  CEF_REQUIRE_UI_THREAD();
  std::string tab_id = TabIdForContentBrowser(browser);
  if (tab_id.empty()) {
    tab_id = active_tab_id_;
  }
  if (tab_id.empty()) {
    return;
  }

  Json state = media.is_object() ? media : Json::object();
  state["tabId"] = tab_id;
  const bool active = state.value("active", false);
  const bool playing = state.value("playing", false);
  if (!active && !playing) {
    tab_media_.erase(tab_id);
  } else {
    tab_media_[tab_id] = state;
  }

  Json event = state;
  event["type"] = "media";
  EmitBrowserEvent(event);

  const bool audible = state.value("audible", playing);
  SetContentAudioPlaying(browser, audible);
}

Json OmniHandler::ContentMediaJson(const std::string& tab_id) const {
  if (tab_id.empty()) {
    return Json::object();
  }
  const auto it = tab_media_.find(tab_id);
  if (it == tab_media_.end()) {
    return Json::object();
  }
  return it->second;
}

bool OmniHandler::ContentMediaControl(const std::string& tab_id,
                                      const std::string& action,
                                      double value) {
  CEF_REQUIRE_UI_THREAD();
  if (action.empty()) {
    return false;
  }
  // Only allow known actions — values are injected as numbers.
  if (action != "toggle" && action != "play" && action != "pause" &&
      action != "seek" && action != "seekRelative" && action != "seekStart" &&
      action != "seekEnd" && action != "pip") {
    return false;
  }
  CefRefPtr<CefBrowserView> view = nullptr;
  if (!tab_id.empty()) {
    view = ContentViewForTab(tab_id);
  }
  if (!view && !active_tab_id_.empty()) {
    view = ContentViewForTab(active_tab_id_);
  }
  if (!view && !tab_content_views_.empty()) {
    view = tab_content_views_.begin()->second;
  }
  if (!view) {
    view = content_browser_view();
  }
  if (!view && !recycled_content_views_.empty()) {
    view = recycled_content_views_.front();
  }
  if (!view) {
    return false;
  }
  auto browser = view->GetBrowser();
  if (!browser) {
    return false;
  }
  auto frame = browser->GetMainFrame();
  if (!frame || !frame->IsValid()) {
    return false;
  }
  // action is already validated against a known-safe whitelist above.
  const std::string& a = action;
  const std::string v = std::to_string(value);
  const std::string script =
      std::string("(function(){"
      "try{"
      "if(typeof window.__omniMediaControl==='function'){") +
      "window.__omniMediaControl('" + a + "'," + v + ");" +
      "return;"
      "}"
      "var v=document.querySelector('video,audio');"
      "if(v){" +
      "if('" + a + "'==='toggle'){if(v.paused){v.play();}else{v.pause();}}" +
      "else if('" + a + "'==='play'){v.play();}" +
      "else if('" + a + "'==='pause'){v.pause();}" +
      "}"
      "var ytp=document.getElementById('movie_player')||document.querySelector('.html5-video-player');"
      "if(ytp){" +
      "if('" + a + "'==='toggle'){if(typeof ytp.getPlayerState==='function'&&ytp.getPlayerState()===1){ytp.pauseVideo();}else if(typeof ytp.playVideo==='function'){ytp.playVideo();}}" +
      "else if('" + a + "'==='play'&&typeof ytp.playVideo==='function'){ytp.playVideo();}" +
      "else if('" + a + "'==='pause'&&typeof ytp.pauseVideo==='function'){ytp.pauseVideo();}" +
      "}" +
      "}catch(e){}"
      "})();";
  frame->ExecuteJavaScript(script, frame->GetURL(), 0);
  return true;
}

int OmniHandler::ContentMemoryMb() const {
#if defined(OS_WIN)
  PROCESS_MEMORY_COUNTERS_EX pmc = {};
  pmc.cb = sizeof(pmc);
  if (!GetProcessMemoryInfo(GetCurrentProcess(),
                            reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&pmc),
                            sizeof(pmc))) {
    return 0;
  }
  const SIZE_T bytes = pmc.WorkingSetSize;
  return static_cast<int>((bytes + (512ull * 1024ull)) / (1024ull * 1024ull));
#else
  return 0;
#endif
}

void OmniHandler::SubscribeBrowserEvents(
    int64_t query_id,
    CefRefPtr<CefMessageRouterBrowserSide::Callback> callback) {
  browser_subscribers_[query_id] = callback;
  callback->Success(ContentStateJson().dump());
}

void OmniHandler::UnsubscribeBrowserEvents(int64_t query_id) {
  browser_subscribers_.erase(query_id);
}

void OmniHandler::EmitBrowserEvent(const Json& event) {
  const std::string payload = event.dump();
  for (auto& entry : browser_subscribers_) {
    if (entry.second) {
      entry.second->Success(payload);
    }
  }
}

void OmniHandler::NotifyAppearanceChanged() {
  const bool dark = ChromeShouldUseDark();
  const std::string theme = dark ? "dark" : "light";
  const std::string pref = AppearancePreference();
  EmitBrowserEvent(Json{{"type", "appearance.changed"},
                        {"preference", pref},
                        {"theme", theme}});
  const std::string js =
      "(function(){var t='" + theme + "';var p='" + pref +
      "';if(window.OmniTheme&&typeof OmniTheme.applyExternal==='function'){"
      "OmniTheme.applyExternal(p,t);return;}"
      "document.documentElement.setAttribute('data-theme',t);"
      "document.documentElement.style.colorScheme=t;"
      "try{localStorage.setItem('omni.appearance',p);}catch(e){}"
      "window.dispatchEvent(new CustomEvent('omni-theme-change',"
      "{detail:{preference:p,theme:t}}));})();";
  for (auto& browser : browsers_) {
    if (!browser) {
      continue;
    }
    CefRefPtr<CefFrame> frame = browser->GetMainFrame();
    if (!frame || !frame->IsValid()) {
      continue;
    }
    const std::string url = frame->GetURL().ToString();
    if (url.rfind("file:", 0) != 0) {
      continue;
    }
    frame->ExecuteJavaScript(js, frame->GetURL(), 0);
  }
}

void OmniHandler::EmitContentEvent(CefRefPtr<CefBrowser> browser, Json event) {
  if (browser &&
      recycling_content_browser_ids_.count(browser->GetIdentifier()) > 0) {
    return;
  }
  const std::string tab_id = TabIdForContentBrowser(browser);
  if (tab_id.empty()) {
    // Recycled/unbound views (about:blank after a tab close) must not
    // retitle or rewrite history on the tab that is still open.
    return;
  }
  event["tabId"] = tab_id;
  EmitBrowserEvent(event);
}

void OmniHandler::ReloadUi() {
  CEF_REQUIRE_UI_THREAD();
  if (!shell_browser_view_) {
    return;
  }
  if (auto browser = shell_browser_view_->GetBrowser()) {
    browser->ReloadIgnoreCache();
  }
  OverlayHide();
  if (overlay_browser_view_) {
    if (auto browser = overlay_browser_view_->GetBrowser()) {
      browser->ReloadIgnoreCache();
    }
  }
}

void OmniHandler::StartHotReloadIfNeeded() {
  if (!IsDevMode()) {
    return;
  }
  hot_reload_.Start(paths::UiRootDir(), [this]() { ReloadUi(); });
}

bool OmniHandler::OnProcessMessageReceived(
    CefRefPtr<CefBrowser> browser,
    CefRefPtr<CefFrame> frame,
    CefProcessId source_process,
    CefRefPtr<CefProcessMessage> message) {
  CEF_REQUIRE_UI_THREAD();
  if (!message_router_) {
    return false;
  }
  return message_router_->OnProcessMessageReceived(browser, frame,
                                                   source_process, message);
}

void OmniHandler::OnTitleChange(CefRefPtr<CefBrowser> browser,
                                const CefString& title) {
  CEF_REQUIRE_UI_THREAD();
  if (IsContentBrowser(browser)) {
    // Cache the title so ContentStateJson() can return it without a round-trip.
    const std::string tab_id = TabIdForContentBrowser(browser);
    if (!tab_id.empty()) {
      tab_titles_[tab_id] = title.ToString();
    }
    EmitContentEvent(browser,
                     Json{{"type", "title"}, {"title", title.ToString()}});
    return;
  }
  if (IsOverlayBrowser(browser)) {
    // Never let the overlay page retitle the window.
    return;
  }
  if (IsDevToolsBrowser(browser)) {
    if (auto browser_view = CefBrowserView::GetForBrowser(browser)) {
      if (auto window = browser_view->GetWindow()) {
        window->SetTitle(title);
      }
    }
    return;
  }

  std::string display = title.ToString();
  if (paths::IsPrivateMode() &&
      (display.empty() || display == "Omni Browser")) {
    display = "Omni Private";
  }
  if (IsDevMode()) {
    display += " [dev · hot reload]";
  }
  if (auto browser_view = CefBrowserView::GetForBrowser(browser)) {
    if (auto window = browser_view->GetWindow()) {
      window->SetTitle(display);
    }
  } else if (alloy_style_) {
    PlatformTitleChange(browser, display);
  }
}

void OmniHandler::OnAddressChange(CefRefPtr<CefBrowser> browser,
                                  CefRefPtr<CefFrame> frame,
                                  const CefString& url) {
  CEF_REQUIRE_UI_THREAD();
  if (!frame || !frame->IsMain() || !IsContentBrowser(browser)) {
    return;
  }
  EmitContentEvent(browser,
                   Json{{"type", "address"},
                        {"url", url.ToString()},
                        {"canGoBack", browser->CanGoBack()},
                        {"canGoForward", browser->CanGoForward()},
                        {"loading", browser->IsLoading()}});
}

bool OmniHandler::OnConsoleMessage(CefRefPtr<CefBrowser> browser,
                                   cef_log_severity_t level,
                                   const CefString& message,
                                   const CefString& source,
                                   int line) {
  CEF_REQUIRE_UI_THREAD();
  std::string msg = message.ToString();
  std::string src = source.ToString();
  std::string pane = IsShellBrowser(browser) ? "[Shell UI]" : (IsOverlayBrowser(browser) ? "[Overlay]" : "[Content Tab]");
  std::string lvl_str = level == LOGSEVERITY_ERROR ? "ERROR" : (level == LOGSEVERITY_WARNING ? "WARN" : "INFO");
  omni::Log("JS Console " + pane + " [" + lvl_str + "] (" + src + ":" + std::to_string(line) + "): " + msg);
  return false;
}

void OmniHandler::OnLoadingStateChange(CefRefPtr<CefBrowser> browser,
                                       bool isLoading,
                                       bool canGoBack,
                                       bool canGoForward) {
  CEF_REQUIRE_UI_THREAD();
  if (!IsContentBrowser(browser)) {
    return;
  }
  std::string url;
  if (auto frame = browser->GetMainFrame()) {
    url = frame->GetURL().ToString();
    if (!isLoading) {
      const int browser_id = browser->GetIdentifier();
      if (recycling_content_browser_ids_.count(browser_id) > 0) {
        if (IsAboutUrl(url)) {
          recycling_content_browser_ids_.erase(browser_id);
        }
      }
      MarkContentSeedIdle(browser);
      const std::string tab_id = TabIdForContentBrowser(browser);
      FlushPendingContentUrl(tab_id);
    }
  }
  EmitContentEvent(browser, Json{{"type", "loading"},
                                 {"url", url},
                                 {"loading", isLoading},
                                 {"canGoBack", canGoBack},
                                 {"canGoForward", canGoForward}});
}

void OmniHandler::OnLoadStart(CefRefPtr<CefBrowser> browser,
                              CefRefPtr<CefFrame> frame,
                              TransitionType transition_type) {
  CEF_REQUIRE_UI_THREAD();
  (void)transition_type;
  // CSS-only early pass on the main frame. Scriptlets stay off in the
  // page world (YouTube Trusted Types / CEF instability).
  if (!IsContentBrowser(browser) || !frame || !frame->IsValid() ||
      !frame->IsMain()) {
    return;
  }
  InjectAdblockCosmetics(frame);
}

void OmniHandler::OnLoadEnd(CefRefPtr<CefBrowser> browser,
                            CefRefPtr<CefFrame> frame,
                            int httpStatusCode) {
  CEF_REQUIRE_UI_THREAD();
  (void)httpStatusCode;
  if (!IsContentBrowser(browser) || !frame || !frame->IsValid() ||
      !frame->IsMain()) {
    return;
  }
  MarkContentSeedIdle(browser);
  const std::string tab_id = TabIdForContentBrowser(browser);
  FlushPendingContentUrl(tab_id);
  InjectScrollbarStyles(frame);
  InjectAdblockObservers(frame);
  DevToolsClient::Get().Attach(browser);
}

void OmniHandler::OnAfterCreated(CefRefPtr<CefBrowser> browser) {
  CEF_REQUIRE_UI_THREAD();
  browsers_.push_back(browser);
  TrackBrowserIdentity(browser);

  if (!message_router_) {
    CefMessageRouterConfig config;
    message_router_ = CefMessageRouterBrowserSide::Create(config);
    library_handler_ = CreateLibraryIpcHandler(this);
    message_router_->AddHandler(library_handler_, false);
  }

  if (auto frame = browser->GetMainFrame()) {
    if (IsDevToolsUrl(frame->GetURL().ToString())) {
      RegisterDevToolsBrowser(browser);
    }
  }
  // DevTools is a popup and is never bound to a tab view.
  if (browser->IsPopup() && TabIdForContentBrowser(browser).empty() &&
      !IsShellBrowser(browser) && !IsOverlayBrowser(browser)) {
    RegisterDevToolsBrowser(browser);
  }

  if (IsShellBrowser(browser)) {
    StartHotReloadIfNeeded();
  }

  if (IsDevToolsBrowser(browser)) {
    return;
  }

  if (IsContentBrowser(browser)) {
    if (auto view = CefBrowserView::GetForBrowser(browser)) {
      view->SetVisible(false);
    }
    LayoutContentBrowser();
    // Do not FlushPending here. The seed about:blank is still attaching;
    // OnLoadEnd / OnLoadingStateChange commit the queued URL.
  }
}

bool OmniHandler::DoClose(CefRefPtr<CefBrowser> browser) {
  CEF_REQUIRE_UI_THREAD();
  if (IsDevToolsBrowser(browser)) {
    return false;
  }
  if (IsShellBrowser(browser)) {
    BeginShutdown();
  }
  return false;
}

void OmniHandler::OnBeforeClose(CefRefPtr<CefBrowser> browser) {
  CEF_REQUIRE_UI_THREAD();
  const bool was_devtools = IsDevToolsBrowser(browser);
  const bool was_shell = !was_devtools && IsShellBrowser(browser);

  if (was_shell) {
    BeginShutdown();
    ForceCloseAuxiliaryBrowsers();
  }

  UntrackBrowserIdentity(browser);
  DetachAgentDevTools(browser->GetIdentifier());

  for (auto it = browsers_.begin(); it != browsers_.end(); ++it) {
    if ((*it)->IsSame(browser)) {
      browsers_.erase(it);
      break;
    }
  }

  bool shell_still_open = false;
  for (const auto& b : browsers_) {
    if (IsShellBrowser(b)) {
      shell_still_open = true;
      break;
    }
  }

  if (shutting_down_ && (browsers_.empty() || !shell_still_open)) {
    FinishShutdown();
  }
}

void OmniHandler::OnLoadError(CefRefPtr<CefBrowser> browser,
                              CefRefPtr<CefFrame> frame,
                              ErrorCode errorCode,
                              const CefString& errorText,
                              const CefString& failedUrl) {
  CEF_REQUIRE_UI_THREAD();
  omni::Log("OnLoadError: url=" + std::string(failedUrl) +
            " code=" + std::to_string(errorCode) +
            " text=" + std::string(errorText));
  if (errorCode == ERR_ABORTED) {
    return;
  }
  if (IsDevToolsBrowser(browser)) {
    return;
  }

  if (IsContentBrowser(browser)) {
    if (!frame->IsMain()) {
      return;
    }
    std::stringstream ss;
    ss << "<html><body style='background:#121112;color:#ececec;font-family:"
          "Segoe UI,sans-serif;padding:2rem'>"
          "<h2>Can't reach this page</h2><p>"
       << std::string(failedUrl) << "</p><p>" << std::string(errorText) << " ("
       << errorCode << ")</p></body></html>";
    frame->LoadURL(GetDataURI(ss.str(), "text/html"));
    return;
  }

  std::stringstream ss;
  ss << "<html><body style='background:#1b2838;color:#c7d5e0;font-family:"
        "Segoe UI,sans-serif;padding:2rem'>"
        "<h2>Failed to load UI</h2><p>"
     << std::string(failedUrl) << "</p><p>" << std::string(errorText) << " ("
     << errorCode << ")</p>"
     << "<p>Dev mode loads <code>" << paths::UiRootDir()
     << "</code>. Release expects <code>ui/index.html</code> next to the "
        "executable.</p></body></html>";
  frame->LoadURL(GetDataURI(ss.str(), "text/html"));
}

bool OmniHandler::OnBeforePopup(CefRefPtr<CefBrowser> browser,
                                CefRefPtr<CefFrame> frame,
                                int popup_id,
                                const CefString& target_url,
                                const CefString& target_frame_name,
                                cef_window_open_disposition_t target_disposition,
                                bool user_gesture,
                                const CefPopupFeatures& popupFeatures,
                                CefWindowInfo& windowInfo,
                                CefRefPtr<CefClient>& client,
                                CefBrowserSettings& settings,
                                CefRefPtr<CefDictionaryValue>& extra_info,
                                bool* no_javascript_access) {
  CEF_REQUIRE_UI_THREAD();
  (void)browser;
  (void)frame;
  (void)popup_id;
  (void)target_disposition;
  (void)user_gesture;
  (void)popupFeatures;
  (void)client;
  (void)settings;
  (void)extra_info;
  (void)no_javascript_access;

  const std::string url = target_url.ToString();
  const std::string name = target_frame_name.ToString();
  if (IsDevToolsUrl(url) || name.rfind("DevTools", 0) == 0) {
    windowInfo.runtime_style = CEF_RUNTIME_STYLE_CHROME;
    return false;
  }

  // Never spawn a standalone popup window. Ask the shell to open a new tab
  // instead of navigating the current tab — preserves the page that triggered
  // the popup. The UI layer handles tab creation when it receives "open-tab".
  if (!url.empty() && ShouldOpenInContent(url)) {
    EmitBrowserEvent(
        Json{{"type", "open-tab"}, {"url", url}, {"source", "popup"}});
  }
  return true;
}

bool OmniHandler::OnBeforeBrowse(CefRefPtr<CefBrowser> browser,
                                 CefRefPtr<CefFrame> frame,
                                 CefRefPtr<CefRequest> request,
                                 bool user_gesture,
                                 bool is_redirect) {
  CEF_REQUIRE_UI_THREAD();
  (void)user_gesture;
  (void)is_redirect;
  if (message_router_) {
    message_router_->OnBeforeBrowse(browser, frame);
  }

  // Shell/overlay must stay on local UI. Any web navigation goes to content.
  if ((IsShellBrowser(browser) || IsOverlayBrowser(browser)) && frame &&
      frame->IsMain() && request) {
    const std::string url = request->GetURL().ToString();
    if (ShouldOpenInContent(url)) {
      OpenInContentBrowser(url);
      return true;
    }
  }
  return false;
}

CefRefPtr<CefResourceRequestHandler> OmniHandler::GetResourceRequestHandler(
    CefRefPtr<CefBrowser> browser,
    CefRefPtr<CefFrame> frame,
    CefRefPtr<CefRequest> request,
    bool is_navigation,
    bool is_download,
    const CefString& request_initiator,
    bool& disable_default_handling) {
  (void)frame;
  (void)request;
  (void)is_navigation;
  (void)is_download;
  (void)request_initiator;
  disable_default_handling = false;
  if (request) {
    const std::string url = request->GetURL().ToString();
    if (IsDevToolsUrl(url) || url.rfind("chrome:", 0) == 0) {
      return nullptr;
    }
  }
  // Must not call CefBrowserView APIs here (IO thread).
  if (!ShouldFilterNetwork(browser)) {
    return nullptr;
  }
  return new OmniAdblockResourceHandler(this);
}

bool OmniHandler::OnOpenURLFromTab(CefRefPtr<CefBrowser> browser,
                                   CefRefPtr<CefFrame> frame,
                                   const CefString& target_url,
                                   cef_window_open_disposition_t target_disposition,
                                   bool user_gesture) {
  CEF_REQUIRE_UI_THREAD();
  (void)frame;
  (void)target_disposition;
  (void)user_gesture;

  const std::string url = target_url.ToString();
  if (url.empty()) {
    return true;
  }

  // Ctrl/middle-click, target=_blank, and file→https handoffs.
  if (IsShellBrowser(browser) || IsContentBrowser(browser)) {
    OpenInContentBrowser(url);
    return true;
  }
  return true;
}

void OmniHandler::OnRenderProcessTerminated(CefRefPtr<CefBrowser> browser,
                                            TerminationStatus status,
                                            int error_code,
                                            const CefString& error_string) {
  CEF_REQUIRE_UI_THREAD();
  const std::string reason =
      "Renderer crashed (status=" + std::to_string(status) +
      " error_code=" + std::to_string(error_code) +
      " error_string=" + std::string(error_string) + ")";
  omni::LogCrash("OnRenderProcessTerminated: " + reason);
  FailPendingAgentQueries(reason);
  if (message_router_) {
    message_router_->OnRenderProcessTerminated(browser);
  }
  // Do not Reload() here. Wikipedia-sized pages OOM the renderer; reloading
  // them from this callback often kills the browser process too. Leave the
  // pane; the user or agent can navigate again.
}

void OmniHandler::PlatformTitleChange(CefRefPtr<CefBrowser> browser,
                                      const CefString& title) {
#if defined(OS_WIN)
  CefWindowHandle hwnd = browser->GetHost()->GetWindowHandle();
  if (hwnd) {
    SetWindowTextW(hwnd, title.ToWString().c_str());
  }
#else
  (void)browser;
  (void)title;
#endif
}

void OmniHandler::OnDraggableRegionsChanged(
    CefRefPtr<CefBrowser> browser,
    CefRefPtr<CefFrame> frame,
    const std::vector<CefDraggableRegion>& regions) {
  CEF_REQUIRE_UI_THREAD();
  (void)frame;
  // Only the shell defines window drag regions; the overlay must never
  // clear or override them.
  if (!IsShellBrowser(browser)) {
    return;
  }
  ApplyDraggableRegions(browser, regions);
}

void OmniHandler::ApplyDraggableRegions(
    CefRefPtr<CefBrowser> browser,
    const std::vector<CefDraggableRegion>& regions) {
  auto browser_view = CefBrowserView::GetForBrowser(browser);
  if (!browser_view) {
    return;
  }
  auto window = browser_view->GetWindow();
  if (!window) {
    return;
  }

  std::vector<CefDraggableRegion> window_regions = regions;
  for (auto& region : window_regions) {
    CefPoint origin(region.bounds.x, region.bounds.y);
    browser_view->ConvertPointToWindow(origin);
    region.bounds.x = origin.x;
    region.bounds.y = origin.y;
  }
  window->SetDraggableRegions(window_regions);
}

}  // namespace omni
