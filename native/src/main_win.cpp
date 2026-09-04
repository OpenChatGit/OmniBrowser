#include <windows.h>

#include <string>

#include "include/cef_command_line.h"
#include "include/cef_sandbox_win.h"
#include "omni/log.h"
#include "omni/mcp/mcp_server.h"
#include "omni/omni_app.h"
#include "omni/paths.h"
#include "omni/utf8.h"

#include <cstdlib>

namespace {

bool IsFatalException(DWORD code) {
  switch (code) {
    case EXCEPTION_ACCESS_VIOLATION:
    case EXCEPTION_STACK_OVERFLOW:
    case EXCEPTION_ILLEGAL_INSTRUCTION:
    case EXCEPTION_PRIV_INSTRUCTION:
    case EXCEPTION_IN_PAGE_ERROR:
    case EXCEPTION_ARRAY_BOUNDS_EXCEEDED:
    case EXCEPTION_INT_DIVIDE_BY_ZERO:
    case EXCEPTION_NONCONTINUABLE_EXCEPTION:
    case EXCEPTION_BREAKPOINT:          // CEF CHECK / DebugBreak (0x80000003)
    case 0xC0000374:                    // STATUS_HEAP_CORRUPTION
    case 0xC0000409:                    // STATUS_STACK_BUFFER_OVERRUN / failfast
      return true;
    default:
      return false;
  }
}

LONG CALLBACK CrashHandler(EXCEPTION_POINTERS* ExceptionInfo) {
  const DWORD code = ExceptionInfo->ExceptionRecord->ExceptionCode;
  if (!IsFatalException(code)) {
    return EXCEPTION_CONTINUE_SEARCH;
  }
  char buf[512];
  std::snprintf(
      buf, sizeof(buf),
      "FATAL CRASH: ExceptionCode=0x%08X at Address=%p, Flags=0x%X",
      code, ExceptionInfo->ExceptionRecord->ExceptionAddress,
      ExceptionInfo->ExceptionRecord->ExceptionFlags);
  omni::LogCrash(buf);
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

std::wstring GuiMutexNameForProfile() {
  const std::string id = omni::paths::ProfileInstanceId();
  if (id.empty()) {
    return omni::McpServer::kGuiMutexName;
  }
  return std::wstring(omni::McpServer::kGuiMutexName) + L"." +
         omni::utf8::Widen(id);
}

int ParseMcpPort(CefRefPtr<CefCommandLine> command_line) {
  int port = 8999;
  if (command_line && command_line->HasSwitch("mcp-port")) {
    const std::string port_str =
        command_line->GetSwitchValue("mcp-port").ToString();
    if (!port_str.empty()) {
      port = std::atoi(port_str.c_str());
      if (port <= 0) {
        port = 8999;
      }
    }
  }
  return port;
}

bool WantsMcpStdio(CefRefPtr<CefCommandLine> command_line) {
  return command_line && (command_line->HasSwitch("mcp") ||
                          command_line->HasSwitch("mcp-stdio"));
}

int RunMain(HINSTANCE hInstance, void* sandbox_info) {
  AddVectoredExceptionHandler(1, CrashHandler);
  SetUnhandledExceptionFilter(CrashHandler);

  CefMainArgs main_args(hInstance);
  CefRefPtr<omni::OmniApp> app(new omni::OmniApp);

  const int exit_code = CefExecuteProcess(main_args, app.get(), sandbox_info);
  if (exit_code >= 0) {
    return exit_code;
  }

  CefRefPtr<CefCommandLine> command_line = CefCommandLine::CreateCommandLine();
  command_line->InitFromString(::GetCommandLineW());
  const int mcp_port = ParseMcpPort(command_line);
  const bool mcp_stdio = WantsMcpStdio(command_line);

  // Cursor / Claude spawn OmniBrowser.exe --mcp. This process is the MCP
  // stdio host only — never CEF — so the agent keeps a stable pipe even
  // if the GUI crashes.
  if (mcp_stdio) {
    omni::Log("MCP stdio host starting");
    return omni::McpServer::RunStdioHost(mcp_port);
  }

  // Profile (and therefore the Chromium singleton path) must be known
  // before we take the instance mutex. Private windows get their own
  // cache + mutex so they can coexist with the main GUI.
  ApplyProfileInstanceFromCommandLine();

  const std::wstring mutex_name = GuiMutexNameForProfile();
  HANDLE gui_mutex = CreateMutexW(nullptr, TRUE, mutex_name.c_str());
  const DWORD mutex_err = GetLastError();
  if (gui_mutex && mutex_err == ERROR_ALREADY_EXISTS) {
    omni::Log("OmniBrowser GUI already running — focusing existing window");
    if (omni::McpServer::FocusExistingGuiWindow()) {
      CloseHandle(gui_mutex);
      return 0;
    }
    omni::Log("GUI mutex held but no visible window found — cleaning up stale/headless instance");
    CloseHandle(gui_mutex);
    gui_mutex = nullptr;
    omni::McpServer::TerminateStaleGuiProcesses();
    ::Sleep(300);
    gui_mutex = CreateMutexW(nullptr, TRUE, mutex_name.c_str());
  }

  omni::Log("Main browser process initializing");

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
    omni::Log("CefInitialize failed — focusing already-running instance");
    omni::McpServer::FocusExistingGuiWindow();
    if (gui_mutex) {
      CloseHandle(gui_mutex);
    }
    return 0;
  }

  omni::Log("CefInitialize succeeded. Entering message loop.");
  CefRunMessageLoop();
  omni::Log("CefRunMessageLoop finished. Calling CefShutdown.");
  CefShutdown();
  omni::paths::WipePrivateProfile();
  if (gui_mutex) {
    ReleaseMutex(gui_mutex);
    CloseHandle(gui_mutex);
    gui_mutex = nullptr;
  }
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
