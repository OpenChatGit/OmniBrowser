#pragma once

#include <list>
#include <map>
#include <mutex>
#include <string>
#include <unordered_set>
#include <vector>

#include "include/cef_client.h"
#include "include/cef_download_handler.h"
#include "include/cef_drag_handler.h"
#include "include/cef_focus_handler.h"
#include "include/cef_keyboard_handler.h"
#include "include/cef_resource_request_handler.h"
#include "include/views/cef_browser_view.h"
#include "include/views/cef_menu_button.h"
#include "include/views/cef_overlay_controller.h"
#include "include/views/cef_panel.h"
#include "include/wrapper/cef_message_router.h"
#include "omni/app_menu.h"
#include "omni/app_menu_button.h"
#include "omni/game_launcher.h"
#include "omni/library_json.h"
#include "omni/library_repository.h"
#include "omni/terminal_manager.h"
#include "omni/ui_hot_reload.h"
#include "omni/window_delegate.h"

namespace omni {

class OmniHandler : public CefClient,
                    public CefDisplayHandler,
                    public CefLifeSpanHandler,
                    public CefLoadHandler,
                    public CefRequestHandler,
                    public CefDragHandler,
                    public CefFocusHandler,
                    public CefKeyboardHandler,
                    public CefDownloadHandler {
 public:
  explicit OmniHandler(bool alloy_style);
  ~OmniHandler() override;

  static OmniHandler* GetInstance();

  LibraryRepository& repository() { return repository_; }
  GameLauncher& launcher() { return launcher_; }
  TerminalManager& terminals() { return terminals_; }

  void ReloadUi();

  void SetShellBrowserView(CefRefPtr<CefBrowserView> view);
  void SetContentBrowserView(CefRefPtr<CefBrowserView> view);
  void SetOverlayBrowserView(CefRefPtr<CefBrowserView> view);
  void SetOverlayController(CefRefPtr<CefOverlayController> controller);
  CefRefPtr<CefBrowserView> shell_browser_view() const {
    return shell_browser_view_;
  }
  CefRefPtr<CefBrowserView> content_browser_view() const {
    return ActiveContentView();
  }

  bool IsContentBrowser(CefRefPtr<CefBrowser> browser) const;
  bool IsShellBrowser(CefRefPtr<CefBrowser> browser) const;
  bool IsOverlayBrowser(CefRefPtr<CefBrowser> browser) const;
  /** IO-thread safe: content browsers tracked by CefBrowser identifier. */
  bool ShouldFilterNetwork(CefRefPtr<CefBrowser> browser) const;
  /** Pane registration from OmniBrowserViewDelegate::OnBrowserCreated. */
  void RegisterBrowserPane(CefRefPtr<CefBrowser> browser, BrowserPane pane);
  void UnregisterBrowserPane(CefRefPtr<CefBrowser> browser);

  // Per-tab content browsers: keep inactive pages (and media) alive.
  bool EnsureContentTab(const std::string& tab_id);
  bool ActivateContentTab(const std::string& tab_id);
  void CloseContentTab(const std::string& tab_id);
  std::string ActiveContentTabId() const { return active_tab_id_; }
  std::string TabIdForContentBrowser(CefRefPtr<CefBrowser> browser) const;

  bool ContentNavigate(const std::string& url);
  bool ContentNavigate(const std::string& url, const std::string& tab_id);
  void ContentGoBack();
  void ContentGoForward();
  void ContentReload(bool ignore_cache);
  void ContentStop();
  // Drop the active tab's page without showing the content pane (avoids
  // flashing the previous URL when the next navigation starts from start).
  void ContentClear();
  void SetContentVisible(bool visible);
  void SetChromeHeight(int height);
  int chrome_height() const { return chrome_height_; }
  bool content_visible() const { return content_visible_; }
  void LayoutContentBrowser();
  Json ContentStateJson() const;
  void SetContentAudioPlaying(CefRefPtr<CefBrowser> browser, bool playing);
  void SetContentAudioMuted(bool muted);
  void SetContentAudioMuted(const std::string& tab_id, bool muted);
  bool content_audio_playing() const;
  bool ContentAudioMuted() const;
  bool ContentAudioMuted(const std::string& tab_id) const;
  void SetContentMedia(CefRefPtr<CefBrowser> browser, const Json& media);
  bool ContentMediaControl(const std::string& tab_id,
                           const std::string& action,
                           double value);
  Json ContentMediaJson(const std::string& tab_id) const;

  void SubscribeBrowserEvents(
      int64_t query_id,
      CefRefPtr<CefMessageRouterBrowserSide::Callback> callback);
  void UnsubscribeBrowserEvents(int64_t query_id);

  // Native Views app menu (Chrome/Brave: MenuButton + TOPRIGHT on button bounds).
  void ShowAppMenu(int anchor_left,
                   int anchor_top,
                   int anchor_right,
                   int anchor_bottom,
                   const Json& payload);
  void CancelAppMenu();
  void OnAppMenuClosed();
  void EmitMenuCommand(const Json& command);
  void ToggleDevTools();
  void ShowDevToolsNow();
  void RegisterDevToolsBrowser(CefRefPtr<CefBrowser> browser);
  bool IsDevToolsBrowser(CefRefPtr<CefBrowser> browser) const;
  void ShowHistoryFlyout(const nlohmann::json& recent_tabs);
  void HideHistoryFlyout();
  void HandleOverlayCommand(const Json& command);
  void EmitDownloadProgress();

  // Close content/overlay first, then allow the shell window to die. Call from
  // window chrome close paths so CEF child processes do not linger.
  void BeginShutdown();

  // Tab-tip overlay: a dedicated browser view floated above shell + content
  // via CefOverlayController. Anchored at (anchor_right, anchor_top) in
  // window coordinates; the panel grows downward and to the left.
  void OverlayShow(int anchor_right,
                   int anchor_top,
                   int width,
                   int height,
                   const Json& payload);
  void OverlayResize(int width, int height);
  void OverlayHide();
  void SubscribeOverlayEvents(
      int64_t query_id,
      CefRefPtr<CefMessageRouterBrowserSide::Callback> callback);
  void UnsubscribeOverlayEvents(int64_t query_id);

  CefRefPtr<CefDisplayHandler> GetDisplayHandler() override { return this; }
  CefRefPtr<CefLifeSpanHandler> GetLifeSpanHandler() override { return this; }
  CefRefPtr<CefLoadHandler> GetLoadHandler() override { return this; }
  CefRefPtr<CefRequestHandler> GetRequestHandler() override { return this; }
  CefRefPtr<CefDragHandler> GetDragHandler() override { return this; }
  CefRefPtr<CefFocusHandler> GetFocusHandler() override { return this; }
  CefRefPtr<CefKeyboardHandler> GetKeyboardHandler() override { return this; }
  CefRefPtr<CefDownloadHandler> GetDownloadHandler() override { return this; }

  bool OnBeforeDownload(CefRefPtr<CefBrowser> browser,
                        CefRefPtr<CefDownloadItem> download_item,
                        const CefString& suggested_name,
                        CefRefPtr<CefBeforeDownloadCallback> callback) override;
  void OnDownloadUpdated(CefRefPtr<CefBrowser> browser,
                         CefRefPtr<CefDownloadItem> download_item,
                         CefRefPtr<CefDownloadItemCallback> callback) override;

  void OnGotFocus(CefRefPtr<CefBrowser> browser) override;

  bool OnPreKeyEvent(CefRefPtr<CefBrowser> browser,
                     const CefKeyEvent& event,
                     CefEventHandle os_event,
                     bool* is_keyboard_shortcut) override;

  bool OnProcessMessageReceived(CefRefPtr<CefBrowser> browser,
                                CefRefPtr<CefFrame> frame,
                                CefProcessId source_process,
                                CefRefPtr<CefProcessMessage> message) override;

  void OnTitleChange(CefRefPtr<CefBrowser> browser,
                     const CefString& title) override;
  void OnAddressChange(CefRefPtr<CefBrowser> browser,
                       CefRefPtr<CefFrame> frame,
                       const CefString& url) override;

  void OnAfterCreated(CefRefPtr<CefBrowser> browser) override;
  bool DoClose(CefRefPtr<CefBrowser> browser) override;
  void OnBeforeClose(CefRefPtr<CefBrowser> browser) override;

  bool OnBeforePopup(CefRefPtr<CefBrowser> browser,
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
                     bool* no_javascript_access) override;

  void OnLoadingStateChange(CefRefPtr<CefBrowser> browser,
                            bool isLoading,
                            bool canGoBack,
                            bool canGoForward) override;

  void OnLoadStart(CefRefPtr<CefBrowser> browser,
                   CefRefPtr<CefFrame> frame,
                   TransitionType transition_type) override;

  void OnLoadEnd(CefRefPtr<CefBrowser> browser,
                 CefRefPtr<CefFrame> frame,
                 int httpStatusCode) override;

  void OnLoadError(CefRefPtr<CefBrowser> browser,
                   CefRefPtr<CefFrame> frame,
                   ErrorCode errorCode,
                   const CefString& errorText,
                   const CefString& failedUrl) override;

  bool OnBeforeBrowse(CefRefPtr<CefBrowser> browser,
                      CefRefPtr<CefFrame> frame,
                      CefRefPtr<CefRequest> request,
                      bool user_gesture,
                      bool is_redirect) override;

  CefRefPtr<CefResourceRequestHandler> GetResourceRequestHandler(
      CefRefPtr<CefBrowser> browser,
      CefRefPtr<CefFrame> frame,
      CefRefPtr<CefRequest> request,
      bool is_navigation,
      bool is_download,
      const CefString& request_initiator,
      bool& disable_default_handling) override;

  bool OnOpenURLFromTab(CefRefPtr<CefBrowser> browser,
                        CefRefPtr<CefFrame> frame,
                        const CefString& target_url,
                        cef_window_open_disposition_t target_disposition,
                        bool user_gesture) override;

  void OnRenderProcessTerminated(CefRefPtr<CefBrowser> browser,
                                 TerminationStatus status,
                                 int error_code,
                                 const CefString& error_string) override;

  void OnDraggableRegionsChanged(
      CefRefPtr<CefBrowser> browser,
      CefRefPtr<CefFrame> frame,
      const std::vector<CefDraggableRegion>& regions) override;

  void EmitBrowserEvent(const Json& event);
  void EmitOverlayEvent(const Json& event);

 private:
  void PlatformTitleChange(CefRefPtr<CefBrowser> browser,
                           const CefString& title);
  void StartHotReloadIfNeeded();
  void ApplyDraggableRegions(CefRefPtr<CefBrowser> browser,
                             const std::vector<CefDraggableRegion>& regions);
  void ApplyOverlayBounds(int height);
  void EnsureAppMenuButton();
  void ForceCloseAuxiliaryBrowsers();
  void FinishShutdown();
  CefRefPtr<CefBrowser> ContentBrowser() const;
  CefRefPtr<CefBrowserView> ActiveContentView() const;
  CefRefPtr<CefBrowserView> ContentViewForTab(const std::string& tab_id) const;
  bool ShouldOpenInContent(const std::string& url) const;
  bool OpenInContentBrowser(const std::string& url);
  int ContentMemoryMb() const;
  void EmitContentEvent(CefRefPtr<CefBrowser> browser, Json event);
  void FlushPendingContentUrl(const std::string& tab_id);
  void TrackBrowserIdentity(CefRefPtr<CefBrowser> browser);
  void UntrackBrowserIdentity(CefRefPtr<CefBrowser> browser);

  const bool alloy_style_;
  typedef std::list<CefRefPtr<CefBrowser>> BrowserList;
  BrowserList browsers_;
  bool closing_ = false;
  bool shutting_down_ = false;
  bool quit_posted_ = false;

  CefRefPtr<CefBrowserView> shell_browser_view_;
  // Seed content view created at startup; claimed by the first EnsureContentTab.
  // Also used to recycle closed tab browsers (avoids CloseBrowser/Create races).
  CefRefPtr<CefBrowserView> unbound_content_view_;
  CefRefPtr<CefBrowserView> overlay_browser_view_;
  CefRefPtr<CefOverlayController> overlay_controller_;
  std::map<std::string, CefRefPtr<CefBrowserView>> tab_content_views_;
  std::string active_tab_id_;
  // URL to load once a newly created content browser finishes OnAfterCreated.
  std::map<std::string, std::string> pending_content_urls_;
  // Drop stale events while a recycled view navigates to about:blank.
  std::unordered_set<int> recycling_content_browser_ids_;
  std::map<std::string, bool> tab_audio_playing_;
  std::map<std::string, Json> tab_media_;
  CefRefPtr<CefMenuButton> app_menu_button_;
  CefRefPtr<CefOverlayController> app_menu_button_controller_;
  CefRefPtr<AppMenuButtonHost> app_menu_button_host_;
  int chrome_height_ = 80;
  bool content_visible_ = false;
  bool overlay_visible_ = false;
  bool app_menu_open_ = false;
  bool history_flyout_visible_ = false;
  int overlay_anchor_right_ = 0;
  int overlay_anchor_top_ = 0;
  int overlay_width_ = 244;
  int overlay_height_ = 0;
  int app_menu_btn_right_ = 0;
  int app_menu_btn_bottom_ = 0;
  int app_menu_width_est_ = 248;
  CefRefPtr<AppMenuDelegate> app_menu_;

  std::map<int64_t, CefRefPtr<CefMessageRouterBrowserSide::Callback>>
      browser_subscribers_;
  std::map<int64_t, CefRefPtr<CefMessageRouterBrowserSide::Callback>>
      overlay_subscribers_;

  LibraryRepository repository_;
  GameLauncher launcher_;
  TerminalManager terminals_;
  UiHotReload hot_reload_;

  CefRefPtr<CefMessageRouterBrowserSide> message_router_;
  CefMessageRouterBrowserSide::Handler* library_handler_ = nullptr;

  // CefBrowserView APIs are UI-thread only; network hooks run on the IO thread
  // and must use these identifiers instead of GetForBrowser().
  mutable std::mutex browser_ids_mu_;
  int shell_browser_id_ = -1;
  int overlay_browser_id_ = -1;
  std::unordered_set<int> content_browser_ids_;
  std::unordered_set<int> devtools_browser_ids_;

  IMPLEMENT_REFCOUNTING(OmniHandler);
};

}  // namespace omni
