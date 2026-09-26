#include <csignal>
#include <cstdlib>
#include <fcntl.h>
#include <string>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>

#include <X11/Xlib.h>

#include "include/cef_app.h"
#include "include/cef_command_line.h"
#include "omni/log.h"
#include "omni/mcp/mcp_server.h"
#include "omni/omni_app.h"
#include "omni/paths.h"

namespace {

int g_gui_lock_fd = -1;

bool IsTruthySwitch(CefRefPtr<CefCommandLine> command_line, const char* name) {
  return command_line && command_line->HasSwitch(name);
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

void ApplyProfileInstanceFromCommandLine(CefRefPtr<CefCommandLine> command_line) {
  if (!command_line) {
    return;
  }
  if (command_line->HasSwitch("omni-private")) {
    omni::paths::SetPrivateMode(true);
  }
  if (command_line->HasSwitch("omni-instance")) {
    omni::paths::SetProfileInstanceId(
        command_line->GetSwitchValue("omni-instance").ToString());
  } else if (omni::paths::IsPrivateMode()) {
    omni::paths::SetProfileInstanceId(
        "p" + std::to_string(omni::paths::NowTickMs()) + "x" +
        std::to_string(::getpid()));
  }
}

std::string GuiLockPath() {
  const std::string id = omni::paths::ProfileInstanceId();
  std::string path = omni::paths::EnsureAppDataDir() + "/gui";
  if (!id.empty()) {
    path += "." + id;
  }
  path += ".lock";
  return path;
}

bool TryAcquireGuiLock() {
  const std::string path = GuiLockPath();
  g_gui_lock_fd = ::open(path.c_str(), O_CREAT | O_RDWR, 0644);
  if (g_gui_lock_fd < 0) {
    return false;
  }
  if (::flock(g_gui_lock_fd, LOCK_EX | LOCK_NB) != 0) {
    ::close(g_gui_lock_fd);
    g_gui_lock_fd = -1;
    return false;
  }
  return true;
}

void ReleaseGuiLock() {
  if (g_gui_lock_fd >= 0) {
    ::flock(g_gui_lock_fd, LOCK_UN);
    ::close(g_gui_lock_fd);
    g_gui_lock_fd = -1;
  }
}

int RunMain(int argc, char* argv[]) {
  ::signal(SIGPIPE, SIG_IGN);
  XInitThreads();

  CefMainArgs main_args(argc, argv);
  CefRefPtr<omni::OmniApp> app(new omni::OmniApp);

  const int exit_code = CefExecuteProcess(main_args, app.get(), nullptr);
  if (exit_code >= 0) {
    return exit_code;
  }

  CefRefPtr<CefCommandLine> command_line = CefCommandLine::CreateCommandLine();
  command_line->InitFromArgv(argc, argv);
  const int mcp_port = ParseMcpPort(command_line);
  const bool mcp_stdio = IsTruthySwitch(command_line, "mcp") ||
                         IsTruthySwitch(command_line, "mcp-stdio");

  if (mcp_stdio) {
    omni::Log("MCP stdio host starting");
    return omni::McpServer::RunStdioHost(mcp_port);
  }

  ApplyProfileInstanceFromCommandLine(command_line);

  if (!TryAcquireGuiLock()) {
    omni::Log("OmniBrowser GUI already running — not starting a second instance");
    return 0;
  }

  omni::Log("Main browser process initializing");

  CefSettings settings;
  settings.no_sandbox = true;
  settings.log_severity = LOGSEVERITY_WARNING;

  {
    const std::string cache = omni::paths::EnsureCacheRootDir();
    CefString(&settings.root_cache_path).FromString(cache);
    CefString(&settings.cache_path).FromString(cache);
    CefString(&settings.log_file).FromString(cache + "/cef.log");
    settings.persist_session_cookies = !omni::paths::IsPrivateMode();
  }

  if (!CefInitialize(main_args, settings, app.get(), nullptr)) {
    omni::Log("CefInitialize failed");
    ReleaseGuiLock();
    return 1;
  }

  omni::Log("CefInitialize succeeded. Entering message loop.");
  CefRunMessageLoop();
  omni::Log("CefRunMessageLoop finished. Calling CefShutdown.");
  CefShutdown();
  omni::paths::WipePrivateProfile();
  ReleaseGuiLock();
  omni::Log("CefShutdown finished. Exiting main process.");
  return 0;
}

}  // namespace

int main(int argc, char* argv[]) {
  return RunMain(argc, argv);
}
