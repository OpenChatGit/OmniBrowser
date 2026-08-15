#include "omni/window_commands.h"

#include <windows.h>

#include <climits>
#include <fstream>
#include <filesystem>
#include <string>
#include <vector>

#include "include/views/cef_browser_view.h"
#include "include/views/cef_window.h"
#include "omni/dev_mode.h"
#include "omni/paths.h"
#include "omni/utf8.h"

namespace omni {
namespace {

using Json = nlohmann::json;

CefRefPtr<CefWindow> WindowForBrowser(CefRefPtr<CefBrowser> browser) {
  if (!browser) {
    return nullptr;
  }
  if (auto view = CefBrowserView::GetForBrowser(browser)) {
    return view->GetWindow();
  }
  return nullptr;
}

std::wstring MakeInstanceId() {
  return std::to_wstring(GetTickCount64()) + L"x" +
         std::to_wstring(GetCurrentProcessId());
}

Json ReadJsonFile(const std::string& path_utf8) {
  const std::filesystem::path path(utf8::Widen(path_utf8));
  std::ifstream in(path, std::ios::binary);
  if (!in) {
    return Json();
  }
  std::string data((std::istreambuf_iterator<char>(in)),
                   std::istreambuf_iterator<char>());
  if (data.empty()) {
    return Json();
  }
  Json parsed = Json::parse(data, nullptr, false);
  if (parsed.is_discarded()) {
    return Json();
  }
  return parsed;
}

bool WriteJsonFile(const std::string& path_utf8, const Json& value) {
  paths::EnsureAppDataDir();
  const std::filesystem::path path(utf8::Widen(path_utf8));
  std::ofstream out(path, std::ios::binary | std::ios::trunc);
  if (!out) {
    return false;
  }
  out << value.dump();
  return static_cast<bool>(out);
}

void DeleteFileUtf8(const std::string& path_utf8) {
  std::error_code ec;
  std::filesystem::remove(std::filesystem::path(utf8::Widen(path_utf8)), ec);
}

// Atomically claim a handshake file so only one process can consume it.
bool ClaimFileUtf8(const std::string& path_utf8, std::wstring* claimed_out) {
  const std::wstring src = utf8::Widen(path_utf8);
  const std::wstring claim =
      src + L".claim." + std::to_wstring(GetCurrentProcessId()) + L"." +
      std::to_wstring(GetTickCount64());
  if (!MoveFileW(src.c_str(), claim.c_str())) {
    return false;
  }
  if (claimed_out) {
    *claimed_out = claim;
  }
  return true;
}

bool LaunchNewAppInstance(bool private_window, std::string* error) {
  wchar_t module[MAX_PATH];
  const DWORD len = GetModuleFileNameW(nullptr, module, MAX_PATH);
  if (len == 0 || len >= MAX_PATH) {
    if (error) {
      *error = "Failed to resolve executable path";
    }
    return false;
  }

  const std::wstring instance =
      (private_window ? L"p" : L"") + MakeInstanceId();
  std::wstring cmd = L"\"";
  cmd += module;
  cmd += L"\" --omni-instance=";
  cmd += instance;
  if (private_window) {
    cmd += L" --omni-private";
  }

  // CreateProcess needs a writable command-line buffer.
  std::vector<wchar_t> cmd_buf(cmd.begin(), cmd.end());
  cmd_buf.push_back(L'\0');

  STARTUPINFOW si = {};
  si.cb = sizeof(si);
  PROCESS_INFORMATION pi = {};

  const BOOL ok = CreateProcessW(module, cmd_buf.data(), nullptr, nullptr,
                                 FALSE, 0, nullptr, nullptr, &si, &pi);
  if (!ok) {
    if (error) {
      *error = "CreateProcess failed (" + std::to_string(GetLastError()) + ")";
    }
    return false;
  }

  CloseHandle(pi.hThread);
  CloseHandle(pi.hProcess);
  return true;
}

bool ValidTabPayload(const Json& tab) {
  return tab.is_object() && tab.contains("history") && tab["history"].is_array();
}

}  // namespace

bool HandleWindowCommand(
    CefRefPtr<CefBrowser> browser,
    const std::string& method,
    const Json& params,
    CefRefPtr<CefMessageRouterBrowserSide::Callback> callback) {
  if (method == "app.info") {
    const bool private_mode = paths::IsPrivateMode();
    callback->Success(Json{{"devMode", IsDevMode()},
                           {"name", private_mode ? "Omni Private" : "Omni Browser"},
                           {"version", "0.1.0"},
                           {"private", private_mode}}
                          .dump());
    return true;
  }

  if (method == "tab.consumePending") {
    if (paths::IsPrivateMode()) {
      callback->Success(Json{{"ok", true}, {"tab", nullptr}}.dump());
      return true;
    }
    std::wstring claimed;
    if (!ClaimFileUtf8(paths::PendingOpenTabPath(), &claimed)) {
      callback->Success(Json{{"ok", true}, {"tab", nullptr}}.dump());
      return true;
    }
    const Json pending = ReadJsonFile(utf8::Narrow(claimed));
    std::error_code ec;
    std::filesystem::remove(std::filesystem::path(claimed), ec);
    if (!pending.is_object() || !ValidTabPayload(pending.value("tab", Json()))) {
      callback->Success(Json{{"ok", true}, {"tab", nullptr}}.dump());
      return true;
    }
    callback->Success(
        Json{{"ok", true}, {"tab", pending.value("tab", Json::object())}}
            .dump());
    return true;
  }

  if (method == "window.cursorPos") {
    POINT pt = {};
    if (!GetCursorPos(&pt)) {
      callback->Failure(500, "GetCursorPos failed");
      return true;
    }
    const bool primary_down =
        (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0;
    callback->Success(Json{{"ok", true},
                           {"x", pt.x},
                           {"y", pt.y},
                           {"primaryDown", primary_down}}
                          .dump());
    return true;
  }

  if (method == "window.new") {
    std::string error;
    if (!LaunchNewAppInstance(false, &error)) {
      callback->Failure(500, error.empty() ? "Failed to open new window" : error);
      return true;
    }
    callback->Success(Json{{"ok", true}}.dump());
    return true;
  }

  if (method == "window.newPrivate") {
    std::string error;
    if (!LaunchNewAppInstance(true, &error)) {
      callback->Failure(500, error.empty() ? "Failed to open private window"
                                           : error);
      return true;
    }
    callback->Success(Json{{"ok", true}, {"private", true}}.dump());
    return true;
  }

  if (method == "window.newWithTab") {
    const Json tab = params.contains("tab") ? params["tab"] : Json();
    if (!ValidTabPayload(tab)) {
      callback->Failure(400, "tab required");
      return true;
    }
    if (!WriteJsonFile(paths::PendingOpenTabPath(), Json{{"tab", tab}})) {
      callback->Failure(500, "Failed to stage tab for new window");
      return true;
    }
    std::string error;
    if (!LaunchNewAppInstance(false, &error)) {
      DeleteFileUtf8(paths::PendingOpenTabPath());
      callback->Failure(500, error.empty() ? "Failed to open new window" : error);
      return true;
    }
    callback->Success(Json{{"ok", true}}.dump());
    return true;
  }

  if (method.rfind("window.", 0) != 0 && method.rfind("tab.", 0) != 0 &&
      method != "app.info") {
    return false;
  }

  if (method.rfind("tab.", 0) == 0 || method == "app.info") {
    callback->Failure(404, "Unknown method: " + method);
    return true;
  }

  CefRefPtr<CefWindow> window = WindowForBrowser(browser);
  if (!window) {
    callback->Failure(500, "Window unavailable");
    return true;
  }

  if (method == "window.minimize") {
    window->Minimize();
    callback->Success(Json{{"ok", true}}.dump());
    return true;
  }

  if (method == "window.toggleMaximize") {
    if (window->IsMaximized()) {
      window->Restore();
    } else {
      window->Maximize();
    }
    callback->Success(Json{{"ok", true},
                           {"maximized", window->IsMaximized()}}
                          .dump());
    return true;
  }

  if (method == "window.close") {
    window->Close();
    callback->Success(Json{{"ok", true}}.dump());
    return true;
  }

  if (method == "window.isMaximized") {
    callback->Success(Json{{"maximized", window->IsMaximized()}}.dump());
    return true;
  }

  callback->Failure(404, "Unknown method: " + method);
  return true;
}

}  // namespace omni
