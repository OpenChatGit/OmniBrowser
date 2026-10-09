#include "omni/window_commands.h"
#include "omni/build_config.h"

#if defined(_WIN32)
#include <windows.h>
#else
#include <cstdlib>
#include <unistd.h>
#include <gdk/gdk.h>
#endif

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

std::string MakeInstanceId() {
  return std::to_string(paths::NowTickMs()) + "x" +
#if defined(_WIN32)
         std::to_string(GetCurrentProcessId());
#else
         std::to_string(::getpid());
#endif
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
bool ClaimFileUtf8(const std::string& path_utf8, std::string* claimed_out) {
  const auto src = std::filesystem::path(utf8::Widen(path_utf8));
  const std::string claim_utf8 =
      path_utf8 + ".claim." + MakeInstanceId();
  const auto claim = std::filesystem::path(utf8::Widen(claim_utf8));
  std::error_code ec;
  std::filesystem::rename(src, claim, ec);
  if (ec) {
    return false;
  }
  if (claimed_out) {
    *claimed_out = claim_utf8;
  }
  return true;
}

bool LaunchNewAppInstance(bool private_window, std::string* error) {
#if defined(_WIN32)
  wchar_t module[MAX_PATH];
  const DWORD len = GetModuleFileNameW(nullptr, module, MAX_PATH);
  if (len == 0 || len >= MAX_PATH) {
    if (error) {
      *error = "Failed to resolve executable path";
    }
    return false;
  }

  const std::wstring instance =
      utf8::Widen((private_window ? "p" : "") + MakeInstanceId());
  std::wstring cmd = L"\"";
  cmd += module;
  cmd += L"\" --omni-instance=";
  cmd += instance;
  if (private_window) {
    cmd += L" --omni-private";
  }

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
#else
  char module[4096];
  const ssize_t len = ::readlink("/proc/self/exe", module, sizeof(module) - 1);
  if (len <= 0) {
    if (error) {
      *error = "Failed to resolve executable path";
    }
    return false;
  }
  module[len] = '\0';
  const std::string instance = (private_window ? "p" : "") + MakeInstanceId();
  const std::string instance_arg = "--omni-instance=" + instance;
  const pid_t pid = ::fork();
  if (pid < 0) {
    if (error) {
      *error = "fork failed";
    }
    return false;
  }
  if (pid == 0) {
    if (private_window) {
      ::execl(module, module, instance_arg.c_str(), "--omni-private",
              static_cast<char*>(nullptr));
    } else {
      ::execl(module, module, instance_arg.c_str(),
              static_cast<char*>(nullptr));
    }
    _exit(127);
  }
  return true;
#endif
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
                           {"version", OMNI_APP_VERSION},
                           {"private", private_mode}}
                          .dump());
    return true;
  }

  if (method == "tab.consumePending") {
    if (paths::IsPrivateMode()) {
      callback->Success(Json{{"ok", true}, {"tab", nullptr}}.dump());
      return true;
    }
    std::string claimed;
    if (!ClaimFileUtf8(paths::PendingOpenTabPath(), &claimed)) {
      callback->Success(Json{{"ok", true}, {"tab", nullptr}}.dump());
      return true;
    }
    const Json pending = ReadJsonFile(claimed);
    std::error_code ec;
    std::filesystem::remove(std::filesystem::path(utf8::Widen(claimed)), ec);
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
#if defined(_WIN32)
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
#else
    int x = 0;
    int y = 0;
    const bool primary_down = false;
    if (GdkDisplay* display = gdk_display_get_default()) {
      if (GdkSeat* seat = gdk_display_get_default_seat(display)) {
        if (GdkDevice* pointer = gdk_seat_get_pointer(seat)) {
          gdk_device_get_position(pointer, nullptr, &x, &y);
        }
      }
    }
    callback->Success(Json{{"ok", true},
                           {"x", x},
                           {"y", y},
                           {"primaryDown", primary_down}}
                          .dump());
#endif
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
