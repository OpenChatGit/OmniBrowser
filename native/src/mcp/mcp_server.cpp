#include "omni/mcp/mcp_server.h"

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <fstream>
#include <functional>
#include <iostream>
#include <memory>
#include <mutex>
#include <sstream>
#include <thread>
#include <unordered_map>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <objidl.h>
#include <gdiplus.h>
#pragma comment(lib, "gdiplus.lib")
#endif

#include "include/base/cef_callback.h"
#include "include/cef_app.h"
#include "include/cef_parser.h"
#include "include/cef_string_visitor.h"
#include "include/cef_task.h"
#include "include/wrapper/cef_closure_task.h"
#include "include/views/cef_browser_view.h"
#include "omni/api/api_dispatcher.h"
#include "omni/agent_api.h"
#include "omni/bookmark_store.h"
#include "omni/history_store.h"
#include "omni/log.h"
#include "omni/omni_handler.h"

namespace omni {

namespace {

#if defined(_WIN32)
static int GetEncoderClsid(const WCHAR* format, CLSID* pClsid) {
  UINT num = 0;
  UINT size = 0;
  Gdiplus::GetImageEncodersSize(&num, &size);
  if (size == 0) return -1;
  auto* pImageCodecInfo = reinterpret_cast<Gdiplus::ImageCodecInfo*>(malloc(size));
  if (!pImageCodecInfo) return -1;
  Gdiplus::GetImageEncoders(num, size, pImageCodecInfo);
  for (UINT j = 0; j < num; ++j) {
    if (wcscmp(pImageCodecInfo[j].MimeType, format) == 0) {
      *pClsid = pImageCodecInfo[j].Clsid;
      free(pImageCodecInfo);
      return j;
    }
  }
  free(pImageCodecInfo);
  return -1;
}

static void EnsureGdiplusStarted() {
  static std::once_flag flag;
  std::call_once(flag, []() {
    Gdiplus::GdiplusStartupInput input;
    ULONG_PTR token;
    Gdiplus::GdiplusStartup(&token, &input, nullptr);
  });
}
#endif

// Thread-safe synchronous responder for ApiDispatcher calls
class SyncApiResponder : public ApiResponder {
 public:
  void Success(const std::string& json) override {
    std::lock_guard<std::mutex> lock(mu_);
    if (!done_) {
      done_ = true;
      result_json_ = json;
      is_success_ = true;
      cv_.notify_all();
    }
  }

  void Failure(int code, const std::string& message) override {
    std::lock_guard<std::mutex> lock(mu_);
    if (!done_) {
      done_ = true;
      error_code_ = code;
      error_message_ = message;
      is_success_ = false;
      cv_.notify_all();
    }
  }

  bool Wait(std::chrono::milliseconds timeout) {
    std::unique_lock<std::mutex> lock(mu_);
    return cv_.wait_for(lock, timeout, [this] { return done_; });
  }

  bool is_success() const { return is_success_; }
  const std::string& result_json() const { return result_json_; }
  const std::string& error_message() const { return error_message_; }

 private:
  std::mutex mu_;
  std::condition_variable cv_;
  bool done_ = false;
  bool is_success_ = false;
  int error_code_ = 0;
  std::string error_message_;
  std::string result_json_;
};

class TextWaitVisitor : public CefStringVisitor {
 public:
  void Visit(const CefString& str) override {
    std::lock_guard<std::mutex> lock(mu_);
    text_ = str.ToString();
    done_ = true;
    cv_.notify_all();
  }

  bool WaitFor(std::chrono::milliseconds timeout) {
    std::unique_lock<std::mutex> lock(mu_);
    return cv_.wait_for(lock, timeout, [this] { return done_; });
  }

  const std::string& text() const { return text_; }

 private:
  std::mutex mu_;
  std::condition_variable cv_;
  bool done_ = false;
  std::string text_;
  IMPLEMENT_REFCOUNTING(TextWaitVisitor);
};

class UiSyncTask : public CefTask {
 public:
  explicit UiSyncTask(std::function<void()> fn) : fn_(std::move(fn)) {}
  void Execute() override {
    if (fn_) fn_();
  }

 private:
  std::function<void()> fn_;
  IMPLEMENT_REFCOUNTING(UiSyncTask);
};

// Post `action` to the CEF UI thread and block until it has actually
// finished. Returning on timeout while the UI task still runs is a
// use-after-free: callers capture stack (`ready`, `tab_id`, responders)
// and WaitForBrowserReady / WaitForTabLoad poll every 400–500ms.
void RunOnUiAndWait(std::function<void()> action,
                    std::chrono::milliseconds timeout =
                        std::chrono::milliseconds(8000)) {
  if (McpServer::Get().IsShuttingDown()) {
    return;
  }
  if (CefCurrentlyOn(TID_UI)) {
    action();
    return;
  }

  struct WaitState {
    std::mutex mu;
    std::condition_variable cv;
    bool done = false;
    std::function<void()> action;
  };
  auto state = std::make_shared<WaitState>();
  state->action = std::move(action);

  CefPostTask(TID_UI, new UiSyncTask([state]() {
                if (state->action) {
                  state->action();
                }
                {
                  std::lock_guard<std::mutex> lock(state->mu);
                  state->done = true;
                }
                state->cv.notify_all();
              }));

  std::unique_lock<std::mutex> lock(state->mu);
  if (!state->cv.wait_for(lock, timeout, [state] { return state->done; })) {
    Log("RunOnUiAndWait: UI task slower than timeout, waiting until it finishes");
    state->cv.wait(lock, [state] { return state->done; });
  }
}

struct AgentSession {
  std::string id;
  std::string name;
  std::chrono::steady_clock::time_point last_seen;
};

std::mutex g_agents_mu;
std::unordered_map<std::string, AgentSession> g_agents;

int PruneAndCountAgents() {
  const auto now = std::chrono::steady_clock::now();
  int count = 0;
  std::lock_guard<std::mutex> lock(g_agents_mu);
  for (auto it = g_agents.begin(); it != g_agents.end();) {
    if (now - it->second.last_seen > std::chrono::seconds(90)) {
      it = g_agents.erase(it);
    } else {
      ++count;
      ++it;
    }
  }
  return count;
}

std::string ResolveAgentId(const std::string& agent_id, const Json& req) {
  if (!agent_id.empty()) {
    return agent_id;
  }
  const Json params = req.value("params", Json::object());
  if (params.is_object()) {
    if (params.contains("agentId") && params["agentId"].is_string()) {
      return params["agentId"].get<std::string>();
    }
    const Json meta = params.value("_meta", Json::object());
    if (meta.is_object() && meta.contains("agentId") &&
        meta["agentId"].is_string()) {
      return meta["agentId"].get<std::string>();
    }
    const Json args = params.value("arguments", Json::object());
    if (args.is_object() && args.contains("agentId") &&
        args["agentId"].is_string()) {
      return args["agentId"].get<std::string>();
    }
  }
  return "mcp-agent";
}

std::string NewTabId() {
  return "tab-" + std::to_string(
                      std::chrono::duration_cast<std::chrono::milliseconds>(
                          std::chrono::system_clock::now().time_since_epoch())
                          .count() %
                      1000000);
}

std::string EscapeJs(const std::string& input) {
  std::string out;
  out.reserve(input.size() + 8);
  for (unsigned char c : input) {
    switch (c) {
      case '\\':
        out += "\\\\";
        break;
      case '\'':
        out += "\\'";
        break;
      case '"':
        out += "\\\"";
        break;
      case '\n':
        out += "\\n";
        break;
      case '\r':
        out += "\\r";
        break;
      default:
        if (c < 0x20) {
          char buf[8];
          std::snprintf(buf, sizeof(buf), "\\u%04x", c);
          out += buf;
        } else {
          out.push_back(static_cast<char>(c));
        }
        break;
    }
  }
  return out;
}

}  // namespace

McpServer& McpServer::Get() {
  static McpServer instance;
  return instance;
}

McpServer::McpServer() = default;
McpServer::~McpServer() {
  Shutdown();
}

void McpServer::Initialize() {
  if (initialized_.exchange(true)) {
    return;
  }
  RegisterTools();
  RegisterResources();
  RegisterPrompts();
  Log("McpServer: Initialized with tools, resources, and prompts.");
}

void McpServer::Shutdown() {
  shutting_down_ = true;
  StopHttpServer();
  StopStdioServer();
  initialized_ = false;
}

void McpServer::RegisterTools() {
  tools_["browser_navigate"] = {
      "browser_navigate",
      "Navigate the active or specified tab to a given URL.",
      {{"type", "object"},
       {"properties",
        {{"url",
          {{"type", "string"}, {"description", "The URL to navigate to"}}},
         {"tabId",
          {{"type", "string"},
           {"description", "Optional tab ID. Defaults to active tab."}}}}},
       {"required", {"url"}}}};

  tools_["browser_list_tabs"] = {
      "browser_list_tabs",
      "List all currently open browser tabs with their IDs, titles, URLs, and active status.",
      {{"type", "object"}, {"properties", Json::object()}}};

  tools_["browser_create_tab"] = {
      "browser_create_tab",
      "Create and open a new tab with an optional initial URL.",
      {{"type", "object"},
       {"properties",
        {{"url",
          {{"type", "string"},
           {"description", "Optional initial URL (defaults to about:blank)"}}},
         {"tabId",
          {{"type", "string"},
           {"description", "Optional specific tab ID to assign"}}}}},
       {"required", Json::array()}}};

  tools_["browser_activate_tab"] = {
      "browser_activate_tab",
      "Switch active focus to a specific tab by tabId.",
      {{"type", "object"},
       {"properties",
        {{"tabId",
          {{"type", "string"},
           {"description", "The tab ID to activate"}}}}},
       {"required", {"tabId"}}}};

  tools_["browser_close_tab"] = {
      "browser_close_tab",
      "Close a browser tab by tabId.",
      {{"type", "object"},
       {"properties",
        {{"tabId",
          {{"type", "string"}, {"description", "The tab ID to close"}}}}},
       {"required", {"tabId"}}}};

  tools_["browser_reload_tab"] = {
      "browser_reload_tab",
      "Reload the current or specified tab.",
      {{"type", "object"},
       {"properties",
        {{"tabId",
          {{"type", "string"},
           {"description", "Optional tab ID (defaults to active tab)"}}},
         {"ignoreCache",
          {{"type", "boolean"},
           {"description", "Whether to ignore cache on reload"}}}}}}};

  tools_["browser_go_back"] = {
      "browser_go_back",
      "Navigate back in history on the active content tab.",
      {{"type", "object"}, {"properties", Json::object()}}};

  tools_["browser_go_forward"] = {
      "browser_go_forward",
      "Navigate forward in history on the active content tab.",
      {{"type", "object"}, {"properties", Json::object()}}};

  tools_["browser_extract_content"] = {
      "browser_extract_content",
      "Extract readable page text (capped) plus title/URL for LLM processing. "
      "Avoids cloning the full DOM so large pages such as Wikipedia cannot OOM.",
      {{"type", "object"},
       {"properties",
        {{"tabId",
          {{"type", "string"},
           {"description", "Optional tab ID (defaults to active tab)"}}}}}}};

  tools_["browser_get_html"] = {
      "browser_get_html",
      "Get a size-capped HTML snapshot of the page or a CSS-selected element. "
      "Never returns a full Wikipedia-sized DOM.",
      {{"type", "object"},
       {"properties",
        {{"tabId",
          {{"type", "string"},
           {"description", "Optional tab ID (defaults to active tab)"}}},
         {"selector",
          {{"type", "string"},
           {"description",
            "Optional CSS selector (defaults to documentElement, truncated)"}}}}}}};

  tools_["browser_eval_js"] = {
      "browser_eval_js",
      "Evaluate JavaScript in the page via the DevTools protocol (not subject "
      "to page CSP / unsafe-eval). Returns the expression result.",
      {{"type", "object"},
       {"properties",
        {{"expression",
          {{"type", "string"},
           {"description", "JavaScript expression to evaluate"}}},
         {"tabId",
          {{"type", "string"},
           {"description", "Optional tab ID (defaults to active tab)"}}}}},
       {"required", {"expression"}}}};

  tools_["browser_click"] = {
      "browser_click",
      "Click an element by CSS selector. A visible agent cursor moves to the "
      "target first.",
      {{"type", "object"},
       {"properties",
        {{"selector",
          {{"type", "string"},
           {"description", "CSS selector of the element to click"}}},
         {"tabId",
          {{"type", "string"},
           {"description", "Optional tab ID (defaults to active tab)"}}}}},
       {"required", {"selector"}}}};

  tools_["browser_fill"] = {
      "browser_fill",
      "Fill or type text into an input or textarea element by CSS selector.",
      {{"type", "object"},
       {"properties",
        {{"selector",
          {{"type", "string"},
           {"description", "CSS selector of the input/textarea"}}},
         {"value",
          {{"type", "string"}, {"description", "The text value to enter"}}},
         {"tabId",
          {{"type", "string"},
           {"description", "Optional tab ID (defaults to active tab)"}}}}},
       {"required", {"selector", "value"}}}};

  tools_["browser_upload_file"] = {
      "browser_upload_file",
      "Set files on a file input without the native OS file dialog. Click the "
      "upload control first (the chooser is intercepted), then call this with "
      "absolute local paths. Optionally pass a CSS selector for an existing "
      "<input type=file>. Uses CDP DOM.setFileInputFiles.",
      {{"type", "object"},
       {"properties",
        {{"files",
          {{"description",
            "Absolute file path, or array of absolute paths"},
           {"oneOf",
            Json::array({Json{{"type", "string"}},
                         Json{{"type", "array"},
                              {"items", Json{{"type", "string"}}}}})}}},
         {"selector",
          {{"type", "string"},
           {"description",
            "Optional CSS selector for <input type=file>. If omitted, uses "
            "the last intercepted file chooser or the last file input."}}},
         {"tabId",
          {{"type", "string"},
           {"description", "Optional tab ID (defaults to active tab)"}}}}},
       {"required", {"files"}}}};

  tools_["browser_scroll"] = {
      "browser_scroll",
      "Scroll the page (top, bottom, or by relative pixel delta).",
      {{"type", "object"},
       {"properties",
        {{"position",
          {{"type", "string"},
           {"description", "Scroll position: 'top', 'bottom', or 'by'"}}},
         {"y",
          {{"type", "integer"},
           {"description", "Vertical pixel scroll amount if position is 'by'"}}},
         {"tabId",
          {{"type", "string"},
           {"description", "Optional tab ID (defaults to active tab)"}}}}}}};

  tools_["browser_get_history"] = {
      "browser_get_history",
      "Retrieve recent browsing history entries.",
      {{"type", "object"},
       {"properties",
        {{"limit",
          {{"type", "integer"},
           {"description", "Max number of items to return (default 50)"}}}}}}};

  tools_["browser_get_bookmarks"] = {
      "browser_get_bookmarks",
      "Retrieve saved bookmarks.",
      {{"type", "object"}, {"properties", Json::object()}}};

  tools_["browser_wait_for_load"] = {
      "browser_wait_for_load",
      "Wait until the active or specified tab finishes loading.",
      {{"type", "object"},
       {"properties",
        {{"tabId",
          {{"type", "string"},
           {"description", "Optional tab ID (defaults to active tab)"}}},
         {"timeoutMs",
          {{"type", "integer"},
           {"description", "Max wait in milliseconds (default 15000)"}}}}}}};

  tools_["browser_status"] = {
      "browser_status",
      "Whether the OmniBrowser window is already open, MCP reachability, "
      "active tab, and connected agent sessions.",
      {{"type", "object"}, {"properties", Json::object()}}};

  tools_["browser_take_screenshot"] = {
      "browser_take_screenshot",
      "Capture a visual screenshot of the active or specified browser tab. "
      "Returns standard MCP image content (PNG/JPEG) and base64 data.",
      {{"type", "object"},
       {"properties",
        {{"tabId",
          {{"type", "string"},
           {"description", "Optional tab ID (defaults to active tab)"}}},
         {"format",
          {{"type", "string"},
           {"enum", {"png", "jpeg"}},
           {"description", "Image format: 'png' or 'jpeg' (default png)"}}},
         {"savePath",
          {{"type", "string"},
           {"description",
            "Optional absolute file path to save the screenshot image file on disk"}}}}}}};

  tools_["browser_screenshot"] = tools_["browser_take_screenshot"];
  tools_["browser_screenshot"].name = "browser_screenshot";

  tools_["browser_toggle_find"] = {
      "browser_toggle_find",
      "Toggle in-page text search find bar overlay floating over web content",
      {{"type", "object"}, {"properties", Json::object()}}};

  tools_["browser_find"] = {
      "browser_find",
      "Search and locate text on the active or specified webpage without opening "
      "the visual find bar. Returns match count and contextual snippets. "
      "Can optionally highlight matches or scroll to the first match.",
      {{"type", "object"},
       {"properties",
        {{"query",
          {{"type", "string"},
           {"description", "The text to search for on the page"}}},
         {"tabId",
          {{"type", "string"},
           {"description", "Optional tab ID (defaults to active tab)"}}},
         {"caseSensitive",
          {{"type", "boolean"},
           {"description", "Case-sensitive search (default false)"}}},
         {"maxMatches",
          {{"type", "integer"},
           {"description", "Max number of match snippets to return (default 25)"}}},
         {"highlight",
          {{"type", "boolean"},
           {"description", "Visually highlight matches on the page (default false)"}}},
         {"scrollTo",
          {{"type", "boolean"},
           {"description", "Scroll the page to the first match (default false)"}}}}},
       {"required", {"query"}}}};
}

void McpServer::RegisterResources() {
  resources_["omni://tabs/active"] = {
      "omni://tabs/active",
      "Active Tab State & Content",
      "Snapshot of the current active browser tab with URL, title, and page details",
      "application/json"};

  resources_["omni://tabs"] = {
      "omni://tabs",
      "Open Tabs List",
      "List of all open tabs in OmniBrowser",
      "application/json"};

  resources_["omni://history/recent"] = {
      "omni://history/recent",
      "Recent Browser History",
      "Recently visited URLs and page titles",
      "application/json"};
}

void McpServer::RegisterPrompts() {
  prompts_["summarize_page"] = {
      "summarize_page",
      "Summarize the content of the currently active browser page",
      {{{"name", "focus"},
        {"description", "Optional focus topic or specific question"},
        {"required", false}}}};

  prompts_["research_topic"] = {
      "research_topic",
      "Research a topic across multiple web searches and pages",
      {{{"name", "topic"},
        {"description", "The research topic"},
        {"required", true}}}};
}

Json McpServer::GetToolsList() const {
  Json tools_array = Json::array();
  for (const auto& [name, def] : tools_) {
    tools_array.push_back({
        {"name", def.name},
        {"description", def.description},
        {"inputSchema", def.input_schema},
    });
  }
  return Json{{"tools", tools_array}};
}

Json McpServer::GetResourcesList() const {
  Json res_array = Json::array();
  for (const auto& [uri, def] : resources_) {
    res_array.push_back({
        {"uri", def.uri},
        {"name", def.name},
        {"description", def.description},
        {"mimeType", def.mime_type},
    });
  }
  return Json{{"resources", res_array}};
}

Json McpServer::GetPromptsList() const {
  Json prompts_array = Json::array();
  for (const auto& [name, def] : prompts_) {
    prompts_array.push_back({
        {"name", def.name},
        {"description", def.description},
        {"arguments", def.arguments},
    });
  }
  return Json{{"prompts", prompts_array}};
}

// Tool execution implementations
Json McpServer::ToolNavigate(const Json& args) {
  const std::string url = args.value("url", "");
  std::string tab_id = args.value("tabId", "");
  if (url.empty()) {
    return Json{{"error", "URL parameter is required"}};
  }
  bool ok = false;
  std::string error;
  RunOnUiAndWait([&]() {
    auto* handler = OmniHandler::GetInstance();
    if (!handler || handler->is_shutting_down()) {
      error = "Browser is shutting down";
      return;
    }
    if (tab_id.empty()) {
      tab_id = handler->ActiveContentTabId();
    }
    bool created = false;
    if (tab_id.empty()) {
      tab_id = NewTabId();
      created = true;
    }
    if (!handler->EnsureContentTab(tab_id)) {
      error = handler->ContentTabCount() >= 16
                  ? "Too many tabs open (max 16)"
                  : "Could not create a content tab";
      return;
    }
    if (created) {
      handler->EmitBrowserEvent(Json{
          {"type", "tab.created"},
          {"tabId", tab_id},
          {"url", url},
          {"activate", true},
      });
    }
    handler->ActivateContentTab(tab_id);
    ok = handler->ContentNavigate(url, tab_id);
    handler->EmitBrowserEvent(Json{
        {"type", "navigate"},
        {"tabId", tab_id},
        {"url", url},
        {"visible", true},
    });
  });
  if (!ok) {
    return Json{{"error", error.empty() ? "Navigation failed" : error},
                {"ok", false},
                {"url", url},
                {"tabId", tab_id}};
  }
  const bool loaded =
      WaitForTabLoad(tab_id, url, std::chrono::milliseconds(15000));
  if (!loaded) {
    return Json{{"error", "Page did not finish loading within 15s"},
                {"ok", false},
                {"url", url},
                {"tabId", tab_id}};
  }
  return Json{{"ok", true}, {"url", url}, {"tabId", tab_id}};
}

Json McpServer::ToolListTabs(const Json& /*args*/) {
  Json tabs = Json::array();
  std::string active_id;
  RunOnUiAndWait([&]() {
    auto* handler = OmniHandler::GetInstance();
    if (!handler) return;
    tabs = handler->GetTabsListJson();
    active_id = handler->ActiveContentTabId();
  });
  return Json{{"tabs", tabs}, {"activeTabId", active_id}};
}

Json McpServer::ToolCreateTab(const Json& args) {
  std::string tab_id = args.value("tabId", "");
  const std::string url = args.value("url", "about:blank");
  if (tab_id.empty()) {
    tab_id = NewTabId();
  }
  bool ok = false;
  std::string error;
  RunOnUiAndWait([&]() {
    auto* handler = OmniHandler::GetInstance();
    if (!handler || handler->is_shutting_down()) {
      error = "Browser is shutting down";
      return;
    }
    ok = handler->EnsureContentTab(tab_id);
    if (!ok) {
      error = handler->ContentTabCount() >= 16
                  ? "Too many tabs open (max 16)"
                  : "Could not create a content tab";
      return;
    }
    handler->ActivateContentTab(tab_id);
    handler->EmitBrowserEvent(Json{
        {"type", "tab.created"},
        {"tabId", tab_id},
        {"url", url},
        {"activate", true},
    });
    if (!url.empty() && url != "about:blank") {
      handler->ContentNavigate(url, tab_id);
    }
  });
  if (!ok) {
    return Json{{"ok", false},
                {"error", error.empty() ? "Failed to create tab" : error},
                {"tabId", tab_id},
                {"url", url}};
  }
  if (!url.empty() && url != "about:blank") {
    WaitForTabLoad(tab_id, url, std::chrono::milliseconds(15000));
  }
  return Json{{"ok", ok}, {"tabId", tab_id}, {"url", url}};
}

Json McpServer::ToolActivateTab(const Json& args) {
  const std::string tab_id = args.value("tabId", "");
  if (tab_id.empty()) {
    return Json{{"error", "tabId required"}};
  }
  bool ok = false;
  RunOnUiAndWait([&]() {
    auto* handler = OmniHandler::GetInstance();
    if (!handler) return;
    ok = handler->ActivateContentTab(tab_id);
    handler->EmitBrowserEvent(Json{
        {"type", "tab.activated"},
        {"tabId", tab_id},
    });
  });
  if (!ok) {
    return Json{{"ok", false}, {"tabId", tab_id}, {"error", "Tab not found"}};
  }
  return Json{{"ok", true}, {"tabId", tab_id}};
}

Json McpServer::ToolCloseTab(const Json& args) {
  const std::string tab_id = args.value("tabId", "");
  if (tab_id.empty()) {
    return Json{{"error", "tabId required"}};
  }
  RunOnUiAndWait([&]() {
    auto* handler = OmniHandler::GetInstance();
    if (!handler) return;
    handler->CloseContentTab(tab_id);
    handler->EmitBrowserEvent(Json{
        {"type", "tab.closed"},
        {"tabId", tab_id},
    });
  });
  return Json{{"ok", true}, {"closedTabId", tab_id}};
}

Json McpServer::ToolReloadTab(const Json& args) {
  const std::string tab_id = args.value("tabId", "");
  const bool ignore_cache = args.value("ignoreCache", false);
  RunOnUiAndWait([&]() {
    auto* handler = OmniHandler::GetInstance();
    if (!handler) return;
    if (!tab_id.empty()) {
      auto view = handler->ContentViewForTab(tab_id);
      if (view && view->GetBrowser()) {
        if (ignore_cache) {
          view->GetBrowser()->ReloadIgnoreCache();
        } else {
          view->GetBrowser()->Reload();
        }
      }
    } else {
      handler->ContentReload(ignore_cache);
    }
  });
  return Json{{"ok", true}};
}

Json McpServer::ToolGoBack(const Json& /*args*/) {
  RunOnUiAndWait([&]() {
    auto* handler = OmniHandler::GetInstance();
    if (handler) handler->ContentGoBack();
  });
  return Json{{"ok", true}};
}

Json McpServer::ToolGoForward(const Json& /*args*/) {
  RunOnUiAndWait([&]() {
    auto* handler = OmniHandler::GetInstance();
    if (handler) handler->ContentGoForward();
  });
  return Json{{"ok", true}};
}

Json McpServer::ToolExtractContent(const Json& args) {
  const std::string tab_id = args.value("tabId", "");
  Json state;
  CefRefPtr<TextWaitVisitor> visitor = new TextWaitVisitor();
  bool asked = false;
  RunOnUiAndWait([&]() {
    auto* handler = OmniHandler::GetInstance();
    if (!handler) {
      return;
    }
    state = handler->ContentStateJson();
    CefRefPtr<CefBrowserView> view;
    if (!tab_id.empty()) {
      view = handler->ContentViewForTab(tab_id);
    }
    if (!view) {
      view = handler->content_browser_view();
    }
    if (!view) {
      return;
    }
    auto browser = view->GetBrowser();
    if (!browser) {
      return;
    }
    auto frame = browser->GetMainFrame();
    if (!frame || !frame->IsValid()) {
      return;
    }
    frame->GetText(visitor);
    asked = true;
  });

  if (!asked) {
    return Json{{"error", "No active content page found"}, {"state", state}};
  }
  if (!visitor->WaitFor(std::chrono::milliseconds(6000))) {
    return Json{{"error", "Timed out reading page text"}, {"state", state}};
  }

  std::string text = visitor->text();
  const bool truncated = text.size() > 48000;
  if (truncated) {
    text.resize(48000);
    text += "\n…[truncated]";
  }
  return Json{{"ok", true},
              {"url", state.value("url", "")},
              {"title", state.value("title", "")},
              {"textContent", text},
              {"markdown", text},
              {"stats",
               {{"charCount", static_cast<int>(text.size())},
                {"truncated", truncated}}}};
}

Json McpServer::ToolGetHtml(const Json& args) {
  const std::string selector = args.value("selector", "");
  std::string expr =
      "(function(){var C=80000;var s=document.documentElement?"
      "document.documentElement.outerHTML:'';return s.length>C?"
      "s.slice(0,C)+'\\n<!-- truncated -->':s;})()";
  if (!selector.empty()) {
    expr = "(function(){var C=80000;var el=document.querySelector('" +
           EscapeJs(selector) +
           "');var s=el?(el.outerHTML||el.textContent||''):'';return s.length>C?"
           "s.slice(0,C)+'\\n<!-- truncated -->':s;})()";
  }
  Json eval_args = args;
  eval_args["expression"] = expr;
  return ToolEvalJs(eval_args);
}

Json DispatchAndWait(const std::string& method, const Json& args,
                     const char* timeout_error) {
  auto responder = std::make_shared<SyncApiResponder>();
  RunOnUiAndWait([method, args, responder]() {
    ApiContext ctx;
    ctx.owner = OmniHandler::GetInstance();
    ctx.method = method;
    ctx.shared_responder = responder;
    ApiDispatcher::Get().Dispatch(method, ctx, args, *responder);
  });
  if (responder->Wait(std::chrono::milliseconds(8000))) {
    if (responder->is_success()) {
      return Json::parse(responder->result_json(), nullptr, false);
    }
    return Json{{"error", responder->error_message()}};
  }
  return Json{{"error", timeout_error}};
}

Json McpServer::ToolEvalJs(const Json& args) {
  return DispatchAndWait("page.eval", args, "JavaScript evaluation timed out");
}

Json McpServer::ToolClick(const Json& args) {
  return DispatchAndWait("page.click", args, "Click timed out");
}

Json McpServer::ToolFill(const Json& args) {
  return DispatchAndWait("page.fill", args, "Fill timed out");
}

Json McpServer::ToolUploadFile(const Json& args) {
  return DispatchAndWait("page.setFiles", args, "File upload timed out");
}

Json McpServer::ToolScroll(const Json& args) {
  return DispatchAndWait("page.scroll", args, "Scroll timed out");
}

Json McpServer::ToolGetHistory(const Json& /*args*/) {
  return history::List();
}

Json McpServer::ToolGetBookmarks(const Json& /*args*/) {
  return bookmarks::List();
}

Json McpServer::ToolWaitForLoad(const Json& args) {
  std::string tab_id = args.value("tabId", "");
  int timeout_ms = args.value("timeoutMs", 15000);
  if (timeout_ms < 250) timeout_ms = 250;
  if (timeout_ms > 60000) timeout_ms = 60000;
  if (tab_id.empty()) {
    RunOnUiAndWait([&]() {
      auto* handler = OmniHandler::GetInstance();
      if (handler) tab_id = handler->ActiveContentTabId();
    });
  }
  const bool ready =
      WaitForTabLoad(tab_id, "", std::chrono::milliseconds(timeout_ms));
  Json state;
  RunOnUiAndWait([&]() {
    auto* handler = OmniHandler::GetInstance();
    if (handler) state = handler->ContentStateJson();
  });
  if (!ready) {
    return Json{{"error", "Timed out waiting for the tab to finish loading"},
                {"ok", false},
                {"tabId", tab_id},
                {"state", state}};
  }
  return Json{{"ok", true}, {"tabId", tab_id}, {"state", state}};
}

Json McpServer::ToolStatus(const Json& /*args*/) {
  Json state;
  bool ready = false;
  RunOnUiAndWait([&]() {
    auto* handler = OmniHandler::GetInstance();
    if (!handler) return;
    ready = handler->shell_browser_view() != nullptr;
    state = handler->ContentStateJson();
  });
  Json agents = Json::array();
  {
    std::lock_guard<std::mutex> lock(g_agents_mu);
    for (const auto& [id, session] : g_agents) {
      agents.push_back({{"id", session.id}, {"name", session.name}});
    }
  }
  return Json{
      {"ok", ready},
      {"ready", ready},
      {"alreadyOpen", true},
      {"mcpReachable", true},
      {"agentCount", static_cast<int>(agents.size())},
      {"agents", agents},
      {"state", state},
  };
}

Json McpServer::ToolTakeScreenshot(const Json& args) {
#if defined(_WIN32)
  EnsureGdiplusStarted();
  const std::string tab_id = args.value("tabId", "");
  const std::string format = args.value("format", "png");
  std::string save_path = args.value("savePath", "");
  if (save_path.empty()) {
    save_path = args.value("save_path", "");
  }
  if (save_path.empty()) {
    save_path = args.value("filepath", "");
  }
  if (save_path.empty()) {
    save_path = args.value("filePath", "");
  }
  if (save_path.empty()) {
    save_path = args.value("path", "");
  }

  HWND hwnd = nullptr;
  RunOnUiAndWait([&]() {
    auto* handler = OmniHandler::GetInstance();
    if (!handler) return;
    CefRefPtr<CefBrowserView> view;
    if (!tab_id.empty()) {
      view = handler->ContentViewForTab(tab_id);
    }
    if (!view) {
      view = handler->content_browser_view();
    }
    if (view && view->GetBrowser() && view->GetBrowser()->GetHost()) {
      hwnd = view->GetBrowser()->GetHost()->GetWindowHandle();
    }
    if (!hwnd && handler->shell_browser_view()) {
      if (auto win = handler->shell_browser_view()->GetWindow()) {
        hwnd = win->GetWindowHandle();
      }
    }
  });

  if (!hwnd) {
    return Json{{"error", "No active browser window available to capture"},
                {"ok", false}};
  }

  RECT rc;
  GetClientRect(hwnd, &rc);
  int width = rc.right - rc.left;
  int height = rc.bottom - rc.top;
  if (width <= 0 || height <= 0) {
    width = 1280;
    height = 800;
  }

  HDC hdc_screen = GetDC(hwnd);
  HDC hdc_mem = CreateCompatibleDC(hdc_screen);
  HBITMAP hbm = CreateCompatibleBitmap(hdc_screen, width, height);
  HGDIOBJ old_bm = SelectObject(hdc_mem, hbm);

  // Capture content surface
  BOOL captured = PrintWindow(hwnd, hdc_mem, PW_RENDERFULLCONTENT);
  if (!captured) {
    captured = BitBlt(hdc_mem, 0, 0, width, height, hdc_screen, 0, 0, SRCCOPY);
  }

  IStream* stream = nullptr;
  CreateStreamOnHGlobal(NULL, TRUE, &stream);

  CLSID clsid;
  std::string mime_type = "image/png";
  if (format == "jpeg" || format == "jpg") {
    GetEncoderClsid(L"image/jpeg", &clsid);
    mime_type = "image/jpeg";
  } else {
    GetEncoderClsid(L"image/png", &clsid);
  }

  {
    Gdiplus::Bitmap bitmap(hbm, nullptr);
    bitmap.Save(stream, &clsid, nullptr);
  }

  STATSTG stat;
  stream->Stat(&stat, STATFLAG_NONAME);
  DWORD bytes_size = (DWORD)stat.cbSize.QuadPart;
  std::vector<char> buffer(bytes_size);
  LARGE_INTEGER seek_pos;
  seek_pos.QuadPart = 0;
  stream->Seek(seek_pos, STREAM_SEEK_SET, NULL);
  ULONG bytes_read = 0;
  stream->Read(buffer.data(), bytes_size, &bytes_read);
  stream->Release();

  SelectObject(hdc_mem, old_bm);
  DeleteObject(hbm);
  DeleteDC(hdc_mem);
  ReleaseDC(hwnd, hdc_screen);

  if (buffer.empty()) {
    return Json{{"error", "Failed to encode screenshot"}, {"ok", false}};
  }

  if (!save_path.empty()) {
    std::ofstream out(save_path, std::ios::binary);
    if (out) {
      out.write(buffer.data(), buffer.size());
    }
  }

  std::string b64 = CefBase64Encode(buffer.data(), buffer.size()).ToString();

  return Json{
      {"ok", true},
      {"mimeType", mime_type},
      {"width", width},
      {"height", height},
      {"image_data", b64},
      {"savePath", save_path},
  };
#else
  return Json{{"error", "Screenshot capture not implemented on this OS"},
              {"ok", false}};
#endif
}

Json McpServer::ToolFind(const Json& args) {
  const std::string query = args.value("query", "");
  const bool case_sensitive = args.value("caseSensitive", false);
  const int max_matches = args.value("maxMatches", 25);
  const bool highlight = args.value("highlight", false);
  const bool scroll_to = args.value("scrollTo", false);

  if (query.empty()) {
    return Json{
        {"ok", true},
        {"found", false},
        {"count", 0},
        {"matches", Json::array()},
        {"message", "Empty search query"}
    };
  }

  std::string expr =
      "(function() {"
      "  const query = " + Json(query).dump() + ";"
      "  const caseSensitive = " + (case_sensitive ? "true" : "false") + ";"
      "  const maxMatches = " + std::to_string(max_matches) + ";"
      "  const doHighlight = " + (highlight ? "true" : "false") + ";"
      "  const doScroll = " + (scroll_to ? "true" : "false") + ";"
      R"JS(
  const walker = document.createTreeWalker(document.body, NodeFilter.SHOW_TEXT);
  const textNodes = [];
  let node;
  const q = caseSensitive ? query : query.toLowerCase();
  while ((node = walker.nextNode())) {
    if (!node.nodeValue) continue;
    const p = node.parentElement;
    if (p && (p.tagName === "SCRIPT" || p.tagName === "STYLE" || p.tagName === "NOSCRIPT" || p.closest('#omni-find-host'))) continue;
    const searchIn = caseSensitive ? node.nodeValue : node.nodeValue.toLowerCase();
    if (searchIn.includes(q)) {
      textNodes.push(node);
    }
  }

  const matches = [];
  let totalMatches = 0;

  for (const n of textNodes) {
    const text = n.nodeValue;
    const searchIn = caseSensitive ? text : text.toLowerCase();
    let idx = searchIn.indexOf(q);
    if (idx === -1) continue;

    const frag = doHighlight ? document.createDocumentFragment() : null;
    let lastIdx = 0;

    while (idx !== -1) {
      totalMatches++;
      if (matches.length < maxMatches) {
        const start = Math.max(0, idx - 30);
        const end = Math.min(text.length, idx + query.length + 30);
        const snippet = (start > 0 ? "..." : "") + text.substring(start, end).trim() + (end < text.length ? "..." : "");
        matches.push({ index: totalMatches, snippet: snippet });
      }

      if (doHighlight) {
        if (idx > lastIdx) {
          frag.appendChild(document.createTextNode(text.substring(lastIdx, idx)));
        }
        const mark = document.createElement('mark');
        mark.className = 'omni-match';
        mark.style.cssText = totalMatches === 1
            ? 'background-color:#ff9800!important;color:#1a191a!important;outline:2px solid #e65100!important;border-radius:2px!important;padding:0 1px!important;'
            : 'background-color:#ffe066!important;color:#1a191a!important;border-radius:2px!important;padding:0 1px!important;';
        mark.textContent = text.substring(idx, idx + query.length);
        frag.appendChild(mark);
        lastIdx = idx + query.length;
      }

      idx = searchIn.indexOf(q, idx + query.length);
    }

    if (doHighlight) {
      if (lastIdx < text.length) {
        frag.appendChild(document.createTextNode(text.substring(lastIdx)));
      }
      if (n.parentNode) {
        n.parentNode.replaceChild(frag, n);
      }
    }
  }

  if (doScroll) {
    const firstMark = document.querySelector('mark.omni-match');
    if (firstMark) {
      try { firstMark.scrollIntoView({ block: 'center', behavior: 'smooth' }); } catch(e) {}
    }
  }

  return {
    ok: true,
    found: totalMatches > 0,
    count: totalMatches,
    matches: matches,
    url: location.href,
    title: document.title
  };
})();
)JS";

  Json eval_args = args;
  eval_args["expression"] = expr;
  Json eval_res = ToolEvalJs(eval_args);
  if (eval_res.is_object() && eval_res.contains("result") && eval_res["result"].is_object()) {
    return eval_res["result"];
  }
  return eval_res;
}

void McpServer::TouchAgentSession(const std::string& agent_id,
                                  const std::string& name) {
  const std::string id = agent_id.empty() ? "mcp-agent" : agent_id;
  {
    std::lock_guard<std::mutex> lock(g_agents_mu);
    auto& session = g_agents[id];
    session.id = id;
    if (!name.empty()) {
      session.name = name;
    } else if (session.name.empty()) {
      session.name = id;
    }
    session.last_seen = std::chrono::steady_clock::now();
  }
}

void McpServer::PauseAgents() {
  agents_paused_ = true;
  {
    std::lock_guard<std::mutex> lock(g_agents_mu);
    g_agents.clear();
  }
  FailPendingAgentQueries("Agent control paused by the user");
  RunOnUiAndWait([&]() {
    auto* handler = OmniHandler::GetInstance();
    if (handler) handler->SetAiPaused(true);
  });
}

void McpServer::ResumeAgents() {
  agents_paused_ = false;
  {
    std::lock_guard<std::mutex> lock(g_agents_mu);
    g_agents.clear();
  }
  RunOnUiAndWait([&]() {
    auto* handler = OmniHandler::GetInstance();
    if (handler) handler->SetAiPaused(false);
  });
}

bool McpServer::WaitForBrowserReady(std::chrono::milliseconds timeout) {
  if (shutting_down_) {
    return false;
  }
  const auto deadline = std::chrono::steady_clock::now() + timeout;
  while (!shutting_down_ && std::chrono::steady_clock::now() < deadline) {
    bool ready = false;
    RunOnUiAndWait(
        [&]() {
          auto* handler = OmniHandler::GetInstance();
          ready = handler && handler->shell_browser_view() &&
                  handler->shell_browser_view()->GetBrowser();
        },
        std::chrono::milliseconds(400));
    if (ready) {
      return true;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(120));
  }
  return false;
}

bool McpServer::WaitForTabLoad(const std::string& tab_id,
                               const std::string& expected_url,
                               std::chrono::milliseconds timeout) {
  const auto deadline = std::chrono::steady_clock::now() + timeout;
  bool saw_loading = false;
  while (!shutting_down_ && std::chrono::steady_clock::now() < deadline) {
    bool loading = true;
    bool pending = false;
    std::string url;
    RunOnUiAndWait([&]() {
      auto* handler = OmniHandler::GetInstance();
      if (!handler) return;
      pending = handler->HasPendingContentUrl(tab_id);
      CefRefPtr<CefBrowserView> view;
      if (!tab_id.empty()) {
        view = handler->ContentViewForTab(tab_id);
      }
      if (!view) {
        view = handler->content_browser_view();
      }
      if (!view) return;
      auto browser = view->GetBrowser();
      if (!browser) return;
      loading = browser->IsLoading() || pending;
      if (auto frame = browser->GetMainFrame()) {
        url = frame->GetURL().ToString();
      }
    }, std::chrono::milliseconds(500));
    if (pending) {
      saw_loading = true;
    } else if (loading) {
      saw_loading = true;
    } else if (!url.empty() && url != "about:blank") {
      if (expected_url.empty() || url.find(expected_url) != std::string::npos ||
          expected_url.find(url) != std::string::npos) {
        return true;
      }
      if (saw_loading) {
        return true;
      }
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(120));
  }
  return false;
}

Json McpServer::ExecuteTool(const std::string& name, const Json& args) {
  if (name.empty()) {
    return Json{{"error", "Missing tool name"}};
  }
  if (shutting_down_) {
    return Json{{"error", "Browser is shutting down"}};
  }
  if (agents_paused_) {
    return Json{{"error", "Agent control is paused by the user"}};
  }
    if (!WaitForBrowserReady(std::chrono::milliseconds(20000))) {
    return Json{
        {"error",
         shutting_down_
             ? "Browser is shutting down"
             : "Browser UI is not ready. Omni may still be starting, or the "
               "window failed to open."}};
  }

  const int leftover = PruneAndCountAgents();
  const bool uses_page =
      name == "browser_navigate" || name == "browser_create_tab" ||
      name == "browser_click" || name == "browser_fill" ||
      name == "browser_scroll" || name == "browser_reload_tab" ||
      name == "browser_go_back" || name == "browser_go_forward" ||
      name == "browser_activate_tab" || name == "browser_close_tab" ||
      name == "browser_extract_content" || name == "browser_get_html" ||
      name == "browser_eval_js" || name == "browser_wait_for_load" ||
      name == "browser_upload_file" || name == "browser_find";
  if (uses_page) {
    const int agent_count = std::max(1, leftover);
    RunOnUiAndWait([&]() {
      auto* handler = OmniHandler::GetInstance();
      if (handler) {
        handler->SetAiActive(true, agent_count);
      }
    });
  } else if (leftover == 0) {
    RunOnUiAndWait([]() {
      auto* handler = OmniHandler::GetInstance();
      if (handler && handler->is_ai_active()) {
        handler->SetAiActive(false, 0);
      }
    });
  }

  if (name == "browser_navigate") return ToolNavigate(args);
  if (name == "browser_list_tabs") return ToolListTabs(args);
  if (name == "browser_create_tab") return ToolCreateTab(args);
  if (name == "browser_activate_tab") return ToolActivateTab(args);
  if (name == "browser_close_tab") return ToolCloseTab(args);
  if (name == "browser_reload_tab") return ToolReloadTab(args);
  if (name == "browser_go_back") return ToolGoBack(args);
  if (name == "browser_go_forward") return ToolGoForward(args);
  if (name == "browser_extract_content") return ToolExtractContent(args);
  if (name == "browser_get_html") return ToolGetHtml(args);
  if (name == "browser_eval_js") return ToolEvalJs(args);
  if (name == "browser_click") return ToolClick(args);
  if (name == "browser_fill") return ToolFill(args);
  if (name == "browser_upload_file") return ToolUploadFile(args);
  if (name == "browser_scroll") return ToolScroll(args);
  if (name == "browser_get_history") return ToolGetHistory(args);
  if (name == "browser_get_bookmarks") return ToolGetBookmarks(args);
  if (name == "browser_wait_for_load") return ToolWaitForLoad(args);
  if (name == "browser_status") return ToolStatus(args);
  if (name == "browser_take_screenshot" || name == "browser_screenshot") {
    return ToolTakeScreenshot(args);
  }
  if (name == "browser_find") return ToolFind(args);
  if (name == "browser_toggle_find") {
    RunOnUiAndWait([]() {
      if (auto* handler = OmniHandler::GetInstance()) {
        handler->ToggleFindBar();
      }
    });
    return Json{{"ok", true}};
  }

  return Json{{"error", "Unknown tool: " + name}};
}

Json McpServer::ReadResource(const std::string& uri) {
  if (uri == "omni://tabs/active") {
    Json content_state;
    RunOnUiAndWait([&]() {
      auto* handler = OmniHandler::GetInstance();
      if (handler) content_state = handler->ContentStateJson();
    });
    return Json{
        {"contents",
         {{{"uri", uri},
           {"mimeType", "application/json"},
           {"text", content_state.dump()}}}}};
  }
  if (uri == "omni://tabs") {
    Json tabs;
    RunOnUiAndWait([&]() {
      auto* handler = OmniHandler::GetInstance();
      if (handler) tabs = handler->GetTabsListJson();
    });
    return Json{
        {"contents",
         {{{"uri", uri},
           {"mimeType", "application/json"},
           {"text", tabs.dump()}}}}};
  }
  if (uri == "omni://history/recent") {
    Json hist = history::List();
    return Json{
        {"contents",
         {{{"uri", uri},
           {"mimeType", "application/json"},
           {"text", hist.dump()}}}}};
  }
  return Json{{"error", "Resource not found: " + uri}};
}

Json McpServer::GetPrompt(const std::string& name, const Json& args) {
  if (name == "summarize_page") {
    const std::string focus = args.value("focus", "");
    std::string user_msg = "Please extract and summarize the main content of the active webpage.";
    if (!focus.empty()) {
      user_msg += " Focus specifically on: " + focus;
    }
    return Json{
        {"description", "Summarize the active webpage content"},
        {"messages",
         {{{"role", "user"},
           {"content", {{"type", "text"}, {"text", user_msg}}}}}}};
  }
  if (name == "research_topic") {
    const std::string topic = args.value("topic", "");
    return Json{
        {"description", "Research a topic in the browser"},
        {"messages",
         {{{"role", "user"},
           {"content",
            {{"type", "text"},
             {"text", "Please research the following topic by navigating to relevant search results and summarizing key findings: " +
                          topic}}}}}}};
  }
  return Json{{"error", "Prompt not found: " + name}};
}

Json McpServer::HandleJsonRpcRequest(const Json& req, const std::string& agent_id) {
  if (!req.is_object()) {
    return Json{
        {"jsonrpc", "2.0"},
        {"id", nullptr},
        {"error", {{"code", -32600}, {"message", "Invalid Request"}}}};
  }

  const auto id = req.value("id", Json(nullptr));
  const std::string method = req.value("method", "");
  const Json params = req.value("params", Json::object());
  const std::string resolved_agent = ResolveAgentId(agent_id, req);

  if (shutting_down_ && method != "ping") {
    return Json{
        {"jsonrpc", "2.0"},
        {"id", id},
        {"error",
         {{"code", -32000}, {"message", "Browser is shutting down"}}}};
  }

  if (method == "initialize") {
    std::string name = "mcp-client";
    if (params.is_object()) {
      const Json info = params.value("clientInfo", Json::object());
      if (info.is_object()) {
        name = info.value("name", name);
      }
    }
    TouchAgentSession(resolved_agent, name);
    return Json{
        {"jsonrpc", "2.0"},
        {"id", id},
        {"result",
         {{"protocolVersion", "2024-11-05"},
          {"capabilities",
           {{"tools", Json::object()},
            {"resources", Json::object()},
            {"prompts", Json::object()}}},
          {"serverInfo",
           {{"name", "OmniBrowser-MCP"}, {"version", "1.0.0"}}},
          {"agentId", resolved_agent}}}};
  }

  if (method == "notifications/initialized" ||
      method == "notifications/cancelled") {
    return Json(); // No reply for notifications
  }

  if (method == "logging/setLevel") {
    return Json{{"jsonrpc", "2.0"}, {"id", id}, {"result", Json::object()}};
  }

  if (method == "ping") {
    return Json{{"jsonrpc", "2.0"}, {"id", id}, {"result", Json::object()}};
  }

  if (method == "tools/list") {
    return Json{{"jsonrpc", "2.0"}, {"id", id}, {"result", GetToolsList()}};
  }

  if (method == "tools/call") {
    if (agents_paused_) {
      return Json{{"jsonrpc", "2.0"},
                  {"id", id},
                  {"error", {{"code", -32003},
                             {"message", "Agent control is paused by the user"}}}};
    }
    TouchAgentSession(resolved_agent, "");
    const std::string tool_name = params.value("name", "");
    const Json arguments = params.value("arguments", Json::object());
    const Json exec_result = ExecuteTool(tool_name, arguments);

    if (exec_result.contains("image_data") && exec_result["image_data"].is_string()) {
      return Json{
          {"jsonrpc", "2.0"},
          {"id", id},
          {"result",
           {{"content",
             {{{"type", "text"},
               {"text", "Screenshot captured successfully (" +
                            std::to_string(exec_result.value("width", 0)) + "x" +
                            std::to_string(exec_result.value("height", 0)) +
                            ")."}},
              {{"type", "image"},
               {"data", exec_result["image_data"].get<std::string>()},
               {"mimeType", exec_result.value("mimeType", "image/png")}}}},
            {"isError", false}}}};
    }

    bool is_error = exec_result.contains("error") ||
                    (exec_result.contains("ok") && exec_result["ok"].is_boolean() &&
                     !exec_result["ok"].get<bool>());
    std::string text_output;
    if (is_error && exec_result.contains("error") &&
        exec_result["error"].is_string()) {
      text_output = exec_result["error"].get<std::string>();
      text_output += "\n\n";
      text_output += exec_result.dump(2);
    } else {
      text_output = exec_result.dump(2);
    }

    return Json{
        {"jsonrpc", "2.0"},
        {"id", id},
        {"result",
         {{"content",
           {{{"type", "text"}, {"text", text_output}}}},
          {"isError", is_error}}}};
  }

  if (method == "resources/list") {
    return Json{{"jsonrpc", "2.0"}, {"id", id}, {"result", GetResourcesList()}};
  }

  if (method == "resources/read") {
    const std::string uri = params.value("uri", "");
    const Json read_res = ReadResource(uri);
    if (read_res.contains("error")) {
      return Json{
          {"jsonrpc", "2.0"},
          {"id", id},
          {"error", {{"code", -32002}, {"message", read_res["error"]}}}};
    }
    return Json{{"jsonrpc", "2.0"}, {"id", id}, {"result", read_res}};
  }

  if (method == "prompts/list") {
    return Json{{"jsonrpc", "2.0"}, {"id", id}, {"result", GetPromptsList()}};
  }

  if (method == "prompts/get") {
    const std::string prompt_name = params.value("name", "");
    const Json prompt_args = params.value("arguments", Json::object());
    const Json prompt_res = GetPrompt(prompt_name, prompt_args);
    if (prompt_res.contains("error")) {
      return Json{
          {"jsonrpc", "2.0"},
          {"id", id},
          {"error", {{"code", -32602}, {"message", prompt_res["error"]}}}};
    }
    return Json{{"jsonrpc", "2.0"}, {"id", id}, {"result", prompt_res}};
  }

  return Json{
      {"jsonrpc", "2.0"},
      {"id", id},
      {"error",
       {{"code", -32601}, {"message", "Method not found: " + method}}}};
}

std::string McpServer::ProcessJsonRpc(const std::string& request_raw,
                                      const std::string& agent_id) {
  if (request_raw.empty()) {
    return "";
  }
  Json req = Json::parse(request_raw, nullptr, false);
  if (req.is_discarded()) {
    return Json{
        {"jsonrpc", "2.0"},
        {"id", nullptr},
        {"error", {{"code", -32700}, {"message", "Parse error"}}}}
        .dump();
  }
  Json resp = HandleJsonRpcRequest(req, agent_id);
  if (resp.is_null() || resp.empty()) {
    return "";
  }
  return resp.dump();
}

}  // namespace omni
