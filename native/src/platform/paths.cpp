#include "omni/paths.h"

#include <filesystem>
#include <sstream>

#include <windows.h>
#include <shlobj.h>

#include "omni/build_config.h"
#include "omni/dev_mode.h"
#include "omni/utf8.h"

namespace omni::paths {

namespace {

std::string g_profile_instance_id;

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

}  // namespace

std::string ExecutableDir() {
  wchar_t buf[MAX_PATH];
  const DWORD len = GetModuleFileNameW(nullptr, buf, MAX_PATH);
  if (len == 0 || len >= MAX_PATH) {
    return ".";
  }
  std::filesystem::path p(buf);
  return utf8::Narrow(p.parent_path().wstring());
}

std::string AppDataDir() {
  wchar_t* appdata = nullptr;
  std::string result;
  size_t len = 0;
  if (_wdupenv_s(&appdata, &len, L"APPDATA") == 0 && appdata) {
    std::filesystem::path p(appdata);
    p /= L"OmniBrowser";
    result = utf8::Narrow(p.wstring());
    free(appdata);
  } else {
    result = ExecutableDir() + "\\userdata";
  }
  return result;
}

std::string EnsureAppDataDir() {
  const std::string dir = AppDataDir();
  std::error_code ec;
  std::filesystem::create_directories(
      std::filesystem::path(utf8::Widen(dir)), ec);
  return dir;
}

std::string DatabasePath() {
  return EnsureAppDataDir() + "\\library.db";
}

std::string HistoryPath() {
  return EnsureAppDataDir() + "\\visit_history.json";
}

std::string BookmarksPath() {
  return EnsureAppDataDir() + "\\bookmarks.json";
}

std::string DownloadsPath() {
  return EnsureAppDataDir() + "\\downloads.json";
}

std::string PendingOpenTabPath() {
  return EnsureAppDataDir() + "\\pending_open_tab.json";
}

std::string UserDownloadsDir() {
  PWSTR known = nullptr;
  if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_Downloads, 0, nullptr, &known)) &&
      known) {
    const std::string result = utf8::Narrow(known);
    CoTaskMemFree(known);
    if (!result.empty()) {
      return result;
    }
  }
  return EnsureAppDataDir() + "\\Downloads";
}

std::string EnsureUserDownloadsDir() {
  const std::string dir = UserDownloadsDir();
  std::error_code ec;
  std::filesystem::create_directories(std::filesystem::path(utf8::Widen(dir)),
                                      ec);
  return dir;
}

void SetProfileInstanceId(const std::string& id) {
  g_profile_instance_id = SanitizeInstanceId(id);
}

std::string ProfileInstanceId() {
  return g_profile_instance_id;
}

std::string CacheRootDir() {
  if (!g_profile_instance_id.empty()) {
    return AppDataDir() + "\\instances\\" + g_profile_instance_id + "\\cef";
  }
  return AppDataDir() + "\\cef";
}

std::string EnsureCacheRootDir() {
  const std::string dir = CacheRootDir();
  std::error_code ec;
  std::filesystem::create_directories(
      std::filesystem::path(utf8::Widen(dir)), ec);
  return dir;
}

std::string PathToFileUrl(const std::string& path) {
  std::filesystem::path p(utf8::Widen(path));
  std::error_code ec;
  p = std::filesystem::weakly_canonical(p, ec);
  std::string native = utf8::Narrow(p.wstring());
  for (char& c : native) {
    if (c == '\\') {
      c = '/';
    }
  }
  std::ostringstream oss;
  oss << "file:///" << native;
  return oss.str();
}

std::string UiRootDir() {
  if (IsDevMode()) {
    const std::filesystem::path source(OMNI_UI_SOURCE_DIR);
    std::error_code ec;
    if (std::filesystem::exists(source / "index.html", ec)) {
      return utf8::Narrow(source.wstring());
    }
  }
  return utf8::Narrow(
      (std::filesystem::path(utf8::Widen(ExecutableDir())) / L"ui").wstring());
}

std::string UiEntryUrl() {
  const std::filesystem::path index =
      std::filesystem::path(utf8::Widen(UiRootDir())) / L"index.html";
  return PathToFileUrl(utf8::Narrow(index.wstring()));
}

std::string UiOverlayUrl() {
  const std::filesystem::path overlay =
      std::filesystem::path(utf8::Widen(UiRootDir())) / L"overlay.html";
  return PathToFileUrl(utf8::Narrow(overlay.wstring()));
}

}  // namespace omni::paths
