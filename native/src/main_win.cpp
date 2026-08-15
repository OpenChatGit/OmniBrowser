#include <windows.h>

#include <string>

#include "include/cef_command_line.h"
#include "include/cef_sandbox_win.h"
#include "omni/log.h"
#include "omni/omni_app.h"
#include "omni/paths.h"
#include "omni/utf8.h"

namespace {

LONG WINAPI CrashHandler(EXCEPTION_POINTERS* ExceptionInfo) {
  char buf[512];
  std::snprintf(
      buf, sizeof(buf),
      "FATAL CRASH: ExceptionCode=0x%08X at Address=%p, Flags=0x%X",
      ExceptionInfo->ExceptionRecord->ExceptionCode,
      ExceptionInfo->ExceptionRecord->ExceptionAddress,
      ExceptionInfo->ExceptionRecord->ExceptionFlags);
  omni::Log(buf);
  return EXCEPTION_CONTINUE_SEARCH;
}

void ApplyProfileInstanceFromCommandLine() {
  CefRefPtr<CefCommandLine> command_line = CefCommandLine::CreateCommandLine();
  command_line->InitFromString(::GetCommandLineW());
  if (command_line->HasSwitch("omni-private")) {
    omni::paths::SetPrivateMode(true);
  }
  if (command_line->HasSwitch("omni-instance")) {
    omni::paths::SetProfileInstanceId(
        command_line->GetSwitchValue("omni-instance").ToString());
  } else if (omni::paths::IsPrivateMode()) {
    omni::paths::SetProfileInstanceId(
        "p" + std::to_string(GetTickCount64()) + "x" +
        std::to_string(GetCurrentProcessId()));
  }
}

int RunMain(HINSTANCE hInstance, void* sandbox_info) {
  SetUnhandledExceptionFilter(CrashHandler);

  CefMainArgs main_args(hInstance);
  CefRefPtr<omni::OmniApp> app(new omni::OmniApp);

  const int exit_code = CefExecuteProcess(main_args, app.get(), sandbox_info);
  if (exit_code >= 0) {
    return exit_code;
  }

  omni::Log("Main browser process initializing");
  ApplyProfileInstanceFromCommandLine();

  CefSettings settings;
  settings.no_sandbox = true;
  settings.log_severity = LOGSEVERITY_WARNING;

  {
    const std::wstring cache =
        omni::utf8::Widen(omni::paths::EnsureCacheRootDir());
    CefString(&settings.root_cache_path).FromWString(cache);
    CefString(&settings.cache_path).FromWString(cache);
    CefString(&settings.log_file).FromWString(cache + L"\\cef.log");
    settings.persist_session_cookies = !omni::paths::IsPrivateMode();
  }

  if (!CefInitialize(main_args, settings, app.get(), sandbox_info)) {
    omni::Log("CefInitialize failed!");
    return CefGetExitCode();
  }

  omni::Log("CefInitialize succeeded. Entering message loop.");
  CefRunMessageLoop();
  omni::Log("CefRunMessageLoop finished. Calling CefShutdown.");
  CefShutdown();
  omni::paths::WipePrivateProfile();
  omni::Log("CefShutdown finished. Exiting main process.");
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
