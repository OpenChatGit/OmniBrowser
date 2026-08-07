#include <windows.h>

#include <string>

#include "include/cef_command_line.h"
#include "include/cef_sandbox_win.h"
#include "omni/omni_app.h"
#include "omni/paths.h"
#include "omni/utf8.h"

namespace {

void ApplyProfileInstanceFromCommandLine() {
  CefRefPtr<CefCommandLine> command_line = CefCommandLine::CreateCommandLine();
  command_line->InitFromString(::GetCommandLineW());
  if (command_line->HasSwitch("omni-instance")) {
    omni::paths::SetProfileInstanceId(
        command_line->GetSwitchValue("omni-instance").ToString());
  }
}

int RunMain(HINSTANCE hInstance, void* sandbox_info) {
  CefMainArgs main_args(hInstance);

  CefRefPtr<omni::OmniApp> app(new omni::OmniApp);

  // Sub-processes (renderer, GPU, ...) must receive the same CefApp so the
  // render-side message router is installed.
  const int exit_code = CefExecuteProcess(main_args, app.get(), sandbox_info);
  if (exit_code >= 0) {
    return exit_code;
  }

  ApplyProfileInstanceFromCommandLine();

  CefSettings settings;
  settings.no_sandbox = true;

  // Persist cookies / localStorage / profile data across restarts.
  // root_cache_path alone is not enough — without cache_path CEF runs the
  // global context in incognito (in-memory) mode.
  // Each --omni-instance=id gets its own profile dir (separate process lock).
  {
    const std::wstring cache =
        omni::utf8::Widen(omni::paths::EnsureCacheRootDir());
    CefString(&settings.root_cache_path).FromWString(cache);
    CefString(&settings.cache_path).FromWString(cache);
    settings.persist_session_cookies = true;
  }

  if (!CefInitialize(main_args, settings, app.get(), sandbox_info)) {
    return CefGetExitCode();
  }

  CefRunMessageLoop();
  CefShutdown();
  return 0;
}

}  // namespace

int APIENTRY wWinMain(HINSTANCE hInstance,
                      HINSTANCE hPrevInstance,
                      LPTSTR lpCmdLine,
                      int nCmdShow) {
  UNREFERENCED_PARAMETER(hPrevInstance);
  UNREFERENCED_PARAMETER(lpCmdLine);
  UNREFERENCED_PARAMETER(nCmdShow);

  void* sandbox_info = nullptr;
  return ::RunMain(hInstance, sandbox_info);
}
