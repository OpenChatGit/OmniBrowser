#include "omni/browser_commands.h"

#include "omni/bookmark_store.h"
#include "omni/download_store.h"
#include "omni/history_store.h"
#include "omni/omni_handler.h"
#include "omni/paths.h"
#include "omni/utf8.h"

#include "include/cef_cookie.h"
#include "include/cef_request_context.h"

#if defined(_WIN32)
#include <windows.h>
#include <shellapi.h>
#else
#include <cstdlib>
#include <unistd.h>
#include <filesystem>
#endif

namespace omni {
namespace {

class ClearBrowsingDataCallback final : public CefCompletionCallback,
                                        public CefDeleteCookiesCallback {
 public:
  explicit ClearBrowsingDataCallback(
      CefRefPtr<CefMessageRouterBrowserSide::Callback> callback)
      : callback_(std::move(callback)) {}

  void OnComplete() override { CompletePart(true); }

  void OnComplete(int num_deleted) override {
    deleted_cookies_ = num_deleted;
    CompletePart(true);
  }

  void FailPart() { CompletePart(false); }

 private:
  void CompletePart(bool success) {
    success_ = success_ && success;
    if (--pending_ > 0) {
      return;
    }
    if (!callback_) {
      return;
    }
    if (success_) {
      callback_->Success(Json{{"ok", true},
                              {"history", true},
                              {"cache", true},
                              {"cookies", true},
                              {"deletedCookies", deleted_cookies_}}
                             .dump());
    } else {
      callback_->Failure(500, "Failed to clear some browsing data");
    }
    callback_ = nullptr;
  }

  CefRefPtr<CefMessageRouterBrowserSide::Callback> callback_;
  int pending_ = 3;
  int deleted_cookies_ = 0;
  bool success_ = true;

  IMPLEMENT_REFCOUNTING(ClearBrowsingDataCallback);
};

bool OpenPath(const std::string& path, bool select_in_folder) {
  if (path.empty()) {
    return false;
  }
#if defined(_WIN32)
  const std::wstring wide = utf8::Widen(path);
  if (select_in_folder) {
    const std::wstring arg = L"/select,\"" + wide + L"\"";
    const HINSTANCE result =
        ShellExecuteW(nullptr, L"open", L"explorer.exe", arg.c_str(), nullptr,
                      SW_SHOWNORMAL);
    return reinterpret_cast<INT_PTR>(result) > 32;
  }
  const HINSTANCE result =
      ShellExecuteW(nullptr, L"open", wide.c_str(), nullptr, nullptr,
                    SW_SHOWNORMAL);
  return reinterpret_cast<INT_PTR>(result) > 32;
#else
  std::string target = path;
  if (select_in_folder) {
    const auto parent = std::filesystem::path(path).parent_path();
    if (!parent.empty()) {
      target = parent.string();
    }
  }
  const pid_t pid = ::fork();
  if (pid < 0) {
    return false;
  }
  if (pid == 0) {
    ::execlp("xdg-open", "xdg-open", target.c_str(), static_cast<char*>(nullptr));
    _exit(127);
  }
  return true;
#endif
}

}  // namespace

bool HandleBrowserCommand(
    OmniHandler* owner,
    CefRefPtr<CefBrowser> browser,
    int64_t query_id,
    bool persistent,
    const std::string& method,
    const Json& params,
    CefRefPtr<CefMessageRouterBrowserSide::Callback> callback) {
  const bool is_browser = method.rfind("browser.", 0) == 0;
  const bool is_overlay = method.rfind("overlay.", 0) == 0;
  const bool is_menu = method.rfind("menu.", 0) == 0;
  const bool is_history = method.rfind("history.", 0) == 0;
  const bool is_bookmarks = method.rfind("bookmarks.", 0) == 0;
  const bool is_downloads = method.rfind("downloads.", 0) == 0;
  const bool is_devtools = method.rfind("devtools.", 0) == 0;
  if (!owner ||
      (!is_browser && !is_overlay && !is_menu && !is_history && !is_bookmarks &&
       !is_downloads && !is_devtools)) {
    return false;
  }

  if (method == "browser.subscribe") {
    if (!persistent) {
      callback->Failure(400, "browser.subscribe requires persistent query");
      return true;
    }
    owner->SubscribeBrowserEvents(query_id, callback);
    return true;
  }

  if (method == "browser.ensureTab") {
    const std::string tab_id = params.value("tabId", "");
    if (tab_id.empty()) {
      callback->Failure(400, "tabId required");
      return true;
    }
    if (!owner->EnsureContentTab(tab_id)) {
      callback->Failure(500, "Failed to create tab content browser");
      return true;
    }
    callback->Success(Json{{"ok", true}, {"tabId", tab_id}}.dump());
    return true;
  }

  if (method == "browser.activateTab") {
    const std::string tab_id = params.value("tabId", "");
    if (tab_id.empty()) {
      callback->Failure(400, "tabId required");
      return true;
    }
    owner->ActivateContentTab(tab_id);
    callback->Success(Json{{"ok", true}, {"tabId", tab_id}}.dump());
    return true;
  }

  if (method == "browser.closeTab") {
    const std::string tab_id = params.value("tabId", "");
    if (tab_id.empty()) {
      callback->Failure(400, "tabId required");
      return true;
    }
    owner->CloseContentTab(tab_id);
    callback->Success(Json{{"ok", true}, {"tabId", tab_id}}.dump());
    return true;
  }

  if (method == "browser.navigate") {
    const std::string url = params.value("url", "");
    if (url.empty()) {
      callback->Failure(400, "url required");
      return true;
    }
    const std::string tab_id = params.value("tabId", "");
    const bool ok = tab_id.empty() ? owner->ContentNavigate(url)
                                   : owner->ContentNavigate(url, tab_id);
    if (!ok) {
      callback->Failure(500, "Content browser unavailable");
      return true;
    }
    callback->Success(Json{{"ok", true}}.dump());
    return true;
  }

  if (method == "browser.back") {
    owner->ContentGoBack();
    callback->Success(Json{{"ok", true}}.dump());
    return true;
  }

  if (method == "browser.forward") {
    owner->ContentGoForward();
    callback->Success(Json{{"ok", true}}.dump());
    return true;
  }

  if (method == "browser.reload") {
    const bool ignore_cache = params.value("ignoreCache", false);
    owner->ContentReload(ignore_cache);
    callback->Success(Json{{"ok", true}}.dump());
    return true;
  }

  if (method == "browser.stop") {
    owner->ContentStop();
    callback->Success(Json{{"ok", true}}.dump());
    return true;
  }

  if (method == "browser.clear") {
    owner->ContentClear();
    callback->Success(Json{{"ok", true}}.dump());
    return true;
  }

  if (method == "browser.clearData") {
    history::Clear();

    auto request_context = CefRequestContext::GetGlobalContext();
    auto cookie_manager = request_context
                              ? request_context->GetCookieManager(nullptr)
                              : nullptr;
    if (!request_context || !cookie_manager) {
      callback->Failure(500, "Browser storage is unavailable");
      return true;
    }

    CefRefPtr<ClearBrowsingDataCallback> clear_callback =
        new ClearBrowsingDataCallback(callback);
    if (!cookie_manager->DeleteCookies(CefString(), CefString(),
                                       clear_callback)) {
      clear_callback->FailPart();
    }
    request_context->ClearHttpCache(clear_callback);
    request_context->ClearHttpAuthCredentials(clear_callback);
    return true;
  }

  if (method == "browser.show") {
    owner->SetContentVisible(params.value("visible", true));
    callback->Success(Json{{"ok", true}}.dump());
    return true;
  }

  if (method == "browser.setChromeHeight") {
    owner->SetChromeHeight(params.value("height", 80));
    callback->Success(Json{{"ok", true}}.dump());
    return true;
  }

  if (method == "browser.state") {
    callback->Success(owner->ContentStateJson().dump());
    return true;
  }

  if (method == "browser.audio") {
    owner->SetContentAudioPlaying(browser, params.value("playing", false));
    callback->Success(Json{{"ok", true}}.dump());
    return true;
  }

  if (method == "browser.media") {
    owner->SetContentMedia(browser, params);
    callback->Success(Json{{"ok", true}}.dump());
    return true;
  }

  if (method == "browser.mediaControl") {
    if (!owner->IsShellBrowser(browser) &&
        !owner->IsOverlayBrowser(browser)) {
      callback->Failure(403, "browser.mediaControl is shell-only");
      return true;
    }
    const std::string tab_id = params.value("tabId", "");
    const std::string action = params.value("action", "");
    const double value = params.value("value", 0.0);
    const std::string target =
        tab_id.empty() ? owner->ActiveContentTabId() : tab_id;
    const bool ok = owner->ContentMediaControl(target, action, value);
    callback->Success(Json{{"ok", ok}, {"tabId", target}}.dump());
    return true;
  }

  if (method == "browser.setAudioMuted") {
    const std::string tab_id = params.value("tabId", "");
    const bool muted = params.value("muted", false);
    if (tab_id.empty()) {
      if (owner->ActiveContentTabId().empty()) {
        callback->Failure(404, "No active content tab");
        return true;
      }
      owner->SetContentAudioMuted(muted);
    } else {
      owner->SetContentAudioMuted(tab_id, muted);
    }
    callback->Success(Json{{"ok", true},
                           {"muted", owner->ContentAudioMuted(
                                         tab_id.empty()
                                             ? owner->ActiveContentTabId()
                                             : tab_id)},
                           {"playing", owner->content_audio_playing()}}
                          .dump());
    return true;
  }

  if (method == "browser.find") {
    const std::string text = params.value("text", "");
    const bool forward = params.value("forward", true);
    const bool match_case = params.value("matchCase", false);
    const bool find_next = params.value("findNext", false);
    auto view = owner->content_browser_view();
    auto target_browser = view ? view->GetBrowser() : nullptr;
    if (!target_browser || !target_browser->GetHost()) {
      callback->Failure(404, "No active content browser");
      return true;
    }
    target_browser->GetHost()->Find(text, forward, match_case, find_next);
    callback->Success(Json{{"ok", true}}.dump());
    return true;
  }

  if (method == "browser.stopFinding") {
    const bool clear_selection = params.value("clearSelection", true);
    auto view = owner->content_browser_view();
    auto target_browser = view ? view->GetBrowser() : nullptr;
    if (target_browser && target_browser->GetHost()) {
      target_browser->GetHost()->StopFinding(clear_selection);
    }
    callback->Success(Json{{"ok", true}}.dump());
    return true;
  }

  if (method == "browser.toggleFind") {
    owner->ToggleFindBar();
    callback->Success(Json{{"ok", true}}.dump());
    return true;
  }

  if (method == "browser.showFind") {
    owner->ShowFindBar();
    callback->Success(Json{{"ok", true}}.dump());
    return true;
  }

  if (method == "browser.hideFind") {
    owner->HideFindBar();
    callback->Success(Json{{"ok", true}}.dump());
    return true;
  }

  if (method == "devtools.toggleDock") {
    const bool docked = !owner->IsDevToolsDocked();
    owner->SetDevToolsDocked(docked);
    callback->Success(Json{{"ok", true}, {"docked", docked}}.dump());
    return true;
  }

  if (method == "devtools.isDocked") {
    callback->Success(Json{{"docked", owner->IsDevToolsDocked()}}.dump());
    return true;
  }

  // browser.adblock.* is registered on ApiDispatcher (adblock_api.cpp).

  if (method == "menu.show") {
    const Json payload = params.contains("payload") ? params["payload"]
                                                    : Json::object();
    owner->ShowAppMenu(params.value("anchorLeft", 0),
                       params.value("anchorTop", 0),
                       params.value("anchorRight", 0),
                       params.value("anchorBottom", 0), payload);
    callback->Success(Json{{"ok", true}}.dump());
    return true;
  }

  if (method == "menu.hide") {
    owner->CancelAppMenu();
    callback->Success(Json{{"ok", true}}.dump());
    return true;
  }

  if (method == "overlay.subscribe") {
    if (!persistent) {
      callback->Failure(400, "overlay.subscribe requires persistent query");
      return true;
    }
    owner->SubscribeOverlayEvents(query_id, callback);
    return true;
  }

  if (method == "overlay.show") {
    const Json payload = params.contains("payload") ? params["payload"]
                                                    : Json::object();
    owner->OverlayShow(params.value("anchorRight", 0),
                       params.value("anchorTop", 0),
                       params.value("width", 244),
                       params.value("height", 280), payload);
    callback->Success(Json{{"ok", true}}.dump());
    return true;
  }

  if (method == "overlay.resize") {
    owner->OverlayResize(params.value("width", 0),
                         params.value("height", 280));
    callback->Success(Json{{"ok", true}}.dump());
    return true;
  }

  if (method == "overlay.hide") {
    owner->OverlayHide();
    callback->Success(Json{{"ok", true}}.dump());
    return true;
  }

  if (method == "overlay.command") {
    const Json command = params.contains("command") ? params["command"]
                                                    : params;
    owner->HandleOverlayCommand(command.is_object() ? command : Json::object());
    callback->Success(Json{{"ok", true}}.dump());
    return true;
  }

  if (method == "history.list") {
    const Json entries =
        paths::IsPrivateMode() ? Json::array() : history::List();
    callback->Success(Json{{"ok", true}, {"entries", entries}}.dump());
    return true;
  }

  if (method == "history.record") {
    if (paths::IsPrivateMode()) {
      callback->Success(Json{{"ok", true}, {"entries", Json::array()}}.dump());
      return true;
    }
    const std::string url = params.value("url", "");
    if (url.empty()) {
      callback->Failure(400, "url required");
      return true;
    }
    const Json entries =
        history::Record(url, params.value("title", ""),
                        params.value("ts", static_cast<int64_t>(0)));
    callback->Success(Json{{"ok", true}, {"entries", entries}}.dump());
    return true;
  }

  if (method == "history.remove") {
    if (paths::IsPrivateMode()) {
      callback->Success(Json{{"ok", true},
                             {"removed", false},
                             {"entries", Json::array()}}
                            .dump());
      return true;
    }
    const std::string url = params.value("url", "");
    if (url.empty()) {
      callback->Failure(400, "url required");
      return true;
    }
    const bool removed = history::Remove(url);
    callback->Success(Json{{"ok", true},
                           {"removed", removed},
                           {"entries", history::List()}}
                          .dump());
    return true;
  }

  if (method == "history.clear") {
    if (paths::IsPrivateMode()) {
      callback->Success(Json{{"ok", true}, {"entries", Json::array()}}.dump());
      return true;
    }
    history::Clear();
    callback->Success(Json{{"ok", true}, {"entries", Json::array()}}.dump());
    return true;
  }

  if (method == "history.import") {
    if (paths::IsPrivateMode()) {
      callback->Success(Json{{"ok", true}, {"entries", Json::array()}}.dump());
      return true;
    }
    const Json entries = params.contains("entries") && params["entries"].is_array()
                             ? params["entries"]
                             : Json::array();
    callback->Success(
        Json{{"ok", true}, {"entries", history::Import(entries)}}.dump());
    return true;
  }

  if (method == "bookmarks.list") {
    callback->Success(
        Json{{"ok", true}, {"entries", bookmarks::List()}}.dump());
    return true;
  }

  if (method == "bookmarks.record") {
    const std::string url = params.value("url", "");
    if (url.empty()) {
      callback->Failure(400, "url required");
      return true;
    }
    const Json entries =
        bookmarks::Record(url, params.value("title", ""),
                          params.value("ts", static_cast<int64_t>(0)));
    owner->EmitBrowserEvent(
        Json{{"type", "bookmarks"}, {"action", "record"}, {"url", url}});
    callback->Success(Json{{"ok", true}, {"entries", entries}}.dump());
    return true;
  }

  if (method == "bookmarks.remove") {
    const std::string url = params.value("url", "");
    if (url.empty()) {
      callback->Failure(400, "url required");
      return true;
    }
    const bool removed = bookmarks::Remove(url);
    owner->EmitBrowserEvent(
        Json{{"type", "bookmarks"}, {"action", "remove"}, {"url", url}});
    callback->Success(Json{{"ok", true},
                           {"removed", removed},
                           {"entries", bookmarks::List()}}
                          .dump());
    return true;
  }

  if (method == "bookmarks.clear") {
    bookmarks::Clear();
    owner->EmitBrowserEvent(
        Json{{"type", "bookmarks"}, {"action", "clear"}});
    callback->Success(Json{{"ok", true}, {"entries", Json::array()}}.dump());
    return true;
  }

  if (method == "bookmarks.import") {
    const Json entries = params.contains("entries") && params["entries"].is_array()
                             ? params["entries"]
                             : Json::array();
    const Json result = bookmarks::Import(entries);
    owner->EmitBrowserEvent(
        Json{{"type", "bookmarks"}, {"action", "import"}});
    callback->Success(Json{{"ok", true}, {"entries", result}}.dump());
    return true;
  }

  if (method == "downloads.list") {
    callback->Success(
        Json{{"ok", true}, {"entries", downloads::List()}}.dump());
    return true;
  }

  if (method == "downloads.remove") {
    const std::string id = params.value("id", "");
    if (id.empty()) {
      callback->Failure(400, "id required");
      return true;
    }
    const bool removed = downloads::Remove(id);
    callback->Success(Json{{"ok", true},
                           {"removed", removed},
                           {"entries", downloads::List()}}
                          .dump());
    return true;
  }

  if (method == "downloads.clear") {
    downloads::Clear();
    callback->Success(Json{{"ok", true}, {"entries", Json::array()}}.dump());
    return true;
  }

  if (method == "downloads.open") {
    const std::string path = params.value("path", "");
    if (path.empty()) {
      callback->Failure(400, "path required");
      return true;
    }
    if (!OpenPath(path, false)) {
      callback->Failure(500, "Failed to open file");
      return true;
    }
    callback->Success(Json{{"ok", true}}.dump());
    return true;
  }

  if (method == "downloads.showInFolder") {
    const std::string path = params.value("path", "");
    if (path.empty()) {
      callback->Failure(400, "path required");
      return true;
    }
    if (!OpenPath(path, true)) {
      callback->Failure(500, "Failed to show in folder");
      return true;
    }
    callback->Success(Json{{"ok", true}}.dump());
    return true;
  }

  callback->Failure(404, "Unknown method: " + method);
  return true;
}

}  // namespace omni
