#include "omni/dev_mode.h"

#include <filesystem>
#include <fstream>
#include <string>

#if defined(_WIN32)
#include <windows.h>
#else
#include <cstdlib>
#endif

#include "omni/build_config.h"

namespace omni {
namespace {

#if defined(_WIN32)
bool HasFlag(const std::wstring& needle) {
  const wchar_t* cmd = GetCommandLineW();
  return cmd && wcsstr(cmd, needle.c_str()) != nullptr;
}

bool EnvTruthy(const wchar_t* name) {
  wchar_t* value = nullptr;
  size_t len = 0;
  if (_wdupenv_s(&value, &len, name) != 0 || !value) {
    return false;
  }
  const bool enabled = value[0] == L'1' || value[0] == L'y' || value[0] == L'Y' ||
                       value[0] == L't' || value[0] == L'T';
  free(value);
  return enabled;
}
#else
bool HasFlag(const char* needle) {
  std::ifstream in("/proc/self/cmdline", std::ios::binary);
  if (!in) {
    return false;
  }
  std::string data((std::istreambuf_iterator<char>(in)),
                   std::istreambuf_iterator<char>());
  for (char& c : data) {
    if (c == '\0') {
      c = ' ';
    }
  }
  return data.find(needle) != std::string::npos;
}

bool EnvTruthy(const char* name) {
  const char* value = std::getenv(name);
  if (!value || !value[0]) {
    return false;
  }
  return value[0] == '1' || value[0] == 'y' || value[0] == 'Y' ||
         value[0] == 't' || value[0] == 'T';
}
#endif

bool SourceUiAvailable() {
  std::error_code ec;
  return std::filesystem::exists(
      std::filesystem::path(OMNI_UI_SOURCE_DIR) / "index.html", ec);
}

}  // namespace

bool IsDevMode() {
#if defined(_WIN32)
  if (HasFlag(L"--bundled-ui")) {
    return false;
  }
#else
  if (HasFlag("--bundled-ui")) {
    return false;
  }
#endif
#if !defined(NDEBUG)
  return true;
#else
#if defined(_WIN32)
  if (HasFlag(L"--dev") || EnvTruthy(L"OMNI_DEV")) {
    return true;
  }
#else
  if (HasFlag("--dev") || EnvTruthy("OMNI_DEV")) {
    return true;
  }
#endif
  // Local developer machines: prefer live source UI automatically.
  return SourceUiAvailable();
#endif
}

}  // namespace omni
