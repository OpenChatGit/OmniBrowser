#include "omni/paths.h"

#include <chrono>
#include <filesystem>
#include <sstream>

#if defined(_WIN32)
#include <windows.h>
#include <shlobj.h>
#else
#include <cstdlib>
#include <unistd.h>
#endif

#include "omni/build_config.h"
#include "omni/dev_mode.h"
#include "omni/utf8.h"

namespace omni::paths {

namespace {

std::string g_profile_instance_id;
bool g_private_mode = false;

std::string SanitizeInstanceId(const std::string& raw) {
  std::string out;
  out.reserve(raw.size());
  for (char c : raw) {
    if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
        (c >= '0' && c <= '9') || c == '-' || c == '_') {
      out.push_back(c);
    }
  }
  if (out.size() > 64) {
    out.resize(64);
  }
  return out;
}

std::string FromFs(const std::filesystem::path& p) {
  return utf8::Narrow(p.wstring());
}

std::filesystem::path ToFs(const std::string& utf8) {
  return std::filesystem::path(utf8::Widen(utf8));
}

std::string Join(const std::string& a, const std::string& b) {
  return FromFs(ToFs(a) / utf8::Widen(b));
}

}  // namespace

uint64_t NowTickMs() {
  return static_cast<uint64_t>(
      std::chrono::duration_cast<std::chrono::milliseconds>(
          std::chrono::steady_clock::now().time_since_epoch())
          .count());
}

std::string ExecutableDir() {
#if defined(_WIN32)
  wchar_t buf[MAX_PATH];
  const DWORD len = GetModuleFileNameW(nullptr, buf, MAX_PATH);
  if (len == 0 || len >= MAX_PATH) {
    return ".";
  }
  std::filesystem::path p(buf);
  return FromFs(p.parent_path());
#else
  char buf[4096];
  const ssize_t len = ::readlink("/proc/self/exe", buf, sizeof(buf) - 1);
  if (len <= 0) {
    return ".";
  }
  buf[len] = '\0';
  return FromFs(std::filesystem::path(buf).parent_path());
#endif
}

std::string AppDataDir() {
#if defined(_WIN32)
  wchar_t* appdata = nullptr;
  std::string result;
  size_t len = 0;
  if (_wdupenv_s(&appdata, &len, L"APPDATA") == 0 && appdata) {
    std::filesystem::path p(appdata);
    p /= L"OmniBrowser";
    result = FromFs(p);
    free(appdata);
  } else {
    result = Join(ExecutableDir(), "userdata");
  }
  return result;
#else
  if (const char* xdg = std::getenv("XDG_DATA_HOME"); xdg && xdg[0]) {
    return Join(xdg, "OmniBrowser");
  }
  if (const char* home = std::getenv("HOME"); home && home[0]) {
    return FromFs(ToFs(home) / ".local" / "share" / "OmniBrowser");
  }
  return Join(ExecutableDir(), "userdata");
#endif
}

std::string EnsureAppDataDir() {
  const std::string dir = AppDataDir();
  std::error_code ec;
  std::filesystem::create_directories(ToFs(dir), ec);
  return dir;
}

std::string DatabasePath() {
  return Join(EnsureAppDataDir(), "library.db");
}

std::string HistoryPath() {
  return Join(EnsureAppDataDir(), "visit_history.json");
}

std::string BookmarksPath() {
  return Join(EnsureAppDataDir(), "bookmarks.json");
}

std::string DownloadsPath() {
  return Join(EnsureAppDataDir(), "downloads.json");
}

std::string SettingsPath() {
  return Join(EnsureAppDataDir(), "settings.json");
}

std::string SessionPath() {
  return Join(EnsureAppDataDir(), "tab_session.json");
}

std::string PluginsDir() {
  return Join(UiRootDir(), "plugins");
}

std::string PendingOpenTabPath() {
  return Join(EnsureAppDataDir(), "pending_open_tab.json");
}

std::string UserDownloadsDir() {
#if defined(_WIN32)
  PWSTR known = nullptr;
  if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_Downloads, 0, nullptr, &known)) &&
      known) {
    const std::string result = utf8::Narrow(known);
    CoTaskMemFree(known);
    if (!result.empty()) {
      return result;
    }
  }
  return Join(EnsureAppDataDir(), "Downloads");
#else
  if (const char* home = std::getenv("HOME"); home && home[0]) {
    return FromFs(ToFs(home) / "Downloads");
  }
  return Join(EnsureAppDataDir(), "Downloads");
#endif
}

std::string EnsureUserDownloadsDir() {
  const std::string dir = UserDownloadsDir();
  std::error_code ec;
  std::filesystem::create_directories(ToFs(dir), ec);
  return dir;
}

void SetProfileInstanceId(const std::string& id) {
  g_profile_instance_id = SanitizeInstanceId(id);
}

std::string ProfileInstanceId() {
  return g_profile_instance_id;
}

void SetPrivateMode(bool on) {
  g_private_mode = on;
}

bool IsPrivateMode() {
  return g_private_mode;
}

void WipePrivateProfile() {
  if (!g_private_mode || g_profile_instance_id.empty()) {
    return;
  }
  std::error_code ec;
  std::filesystem::remove_all(
      ToFs(AppDataDir()) / "instances" / g_profile_instance_id, ec);
}

std::string CacheRootDir() {
  if (!g_profile_instance_id.empty()) {
    return FromFs(ToFs(AppDataDir()) / "instances" / g_profile_instance_id /
                  "cef");
  }
  return Join(AppDataDir(), "cef");
}

std::string EnsureCacheRootDir() {
  const std::string dir = CacheRootDir();
  std::error_code ec;
  std::filesystem::create_directories(ToFs(dir), ec);
  return dir;
}

std::string PathToFileUrl(const std::string& path) {
  std::filesystem::path p = ToFs(path);
  std::error_code ec;
  p = std::filesystem::weakly_canonical(p, ec);
  std::string native = FromFs(p);
  for (char& c : native) {
    if (c == '\\') {
      c = '/';
    }
  }
  std::ostringstream oss;
  if (!native.empty() && native.front() == '/') {
    oss << "file://" << native;
  } else {
    oss << "file:///" << native;
  }
  return oss.str();
}

std::string UiRootDir() {
  if (IsDevMode()) {
    const std::filesystem::path source(OMNI_UI_SOURCE_DIR);
    std::error_code ec;
    if (std::filesystem::exists(source / "index.html", ec)) {
      return FromFs(source);
    }
  }
  return FromFs(ToFs(ExecutableDir()) / "ui");
}

std::string UiEntryUrl() {
  const std::filesystem::path index = ToFs(UiRootDir()) / "index.html";
  std::string url = PathToFileUrl(FromFs(index));
  if (IsPrivateMode()) {
    url += "?private=1";
  }
  return url;
}

std::string UiOverlayUrl() {
  const std::filesystem::path overlay = ToFs(UiRootDir()) / "overlay.html";
  return PathToFileUrl(FromFs(overlay));
}

std::string AdblockDir() {
  return Join(AppDataDir(), "adblock");
}

std::string EnsureAdblockDir() {
  const std::string dir = AdblockDir();
  std::error_code ec;
  std::filesystem::create_directories(ToFs(dir), ec);
  return dir;
}

std::string AdblockPrefsPath() {
  return Join(EnsureAdblockDir(), "prefs.json");
}

std::string BundledAdblockDir() {
  const auto beside_exe = ToFs(ExecutableDir()) / "adblock";
  std::error_code ec;
  if (std::filesystem::exists(beside_exe / "omni-baseline.txt", ec)) {
    return FromFs(beside_exe);
  }
  const auto from_ui = ToFs(UiRootDir()).parent_path() / "resources" / "adblock";
  if (std::filesystem::exists(from_ui / "omni-baseline.txt", ec)) {
    return FromFs(from_ui);
  }
  return FromFs(beside_exe);
}

}  // namespace omni::paths
