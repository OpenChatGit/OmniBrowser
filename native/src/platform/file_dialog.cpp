#include "omni/file_dialog.h"

#if defined(_WIN32)
#include <windows.h>
#include <commdlg.h>
#include "omni/utf8.h"
#else
#include <array>
#include <cstdio>
#endif

namespace omni {

std::string PickExecutableDialog() {
#if defined(_WIN32)
  wchar_t file[MAX_PATH] = {0};

  OPENFILENAMEW ofn{};
  ofn.lStructSize = sizeof(ofn);
  ofn.hwndOwner = GetActiveWindow();
  ofn.lpstrFile = file;
  ofn.nMaxFile = MAX_PATH;
  ofn.lpstrFilter = L"Executables (*.exe)\0*.exe\0All Files (*.*)\0*.*\0";
  ofn.nFilterIndex = 1;
  ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST | OFN_NOCHANGEDIR;
  ofn.lpstrTitle = L"Select game executable";

  if (!GetOpenFileNameW(&ofn)) {
    return {};
  }
  return utf8::Narrow(file);
#else
  FILE* pipe = popen(
      "zenity --file-selection --title='Select game executable' 2>/dev/null",
      "r");
  if (!pipe) {
    return {};
  }
  std::array<char, 4096> buf{};
  std::string result;
  if (fgets(buf.data(), static_cast<int>(buf.size()), pipe)) {
    result = buf.data();
  }
  pclose(pipe);
  while (!result.empty() &&
         (result.back() == '\n' || result.back() == '\r')) {
    result.pop_back();
  }
  return result;
#endif
}

}  // namespace omni
