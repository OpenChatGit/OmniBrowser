#include "omni/file_dialog.h"

#include <windows.h>
#include <commdlg.h>

#include "omni/utf8.h"

namespace omni {

std::string PickExecutableDialog() {
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
}

}  // namespace omni
