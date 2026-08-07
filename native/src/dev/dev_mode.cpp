#include "omni/dev_mode.h"

#include <filesystem>
#include <string>

#include <windows.h>

#include "omni/build_config.h"

namespace omni {
namespace {

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

bool SourceUiAvailable() {
  std::error_code ec;
  return std::filesystem::exists(
      std::filesystem::path(OMNI_UI_SOURCE_DIR) / "index.html", ec);
}

}  // namespace

bool IsDevMode() {
  if (HasFlag(L"--bundled-ui")) {
    return false;
  }
#if !defined(NDEBUG)
  return true;
#else
  if (HasFlag(L"--dev") || EnvTruthy(L"OMNI_DEV")) {
    return true;
  }
  // Local developer machines: prefer live source UI automatically.
  return SourceUiAvailable();
#endif
}

}  // namespace omni
