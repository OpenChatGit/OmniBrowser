#include "omni/game_launcher.h"

#include <filesystem>
#include <vector>

#include "omni/utf8.h"

namespace omni {

GameLauncher::~GameLauncher() {
  std::lock_guard lock(mu_);
  for (auto& [id, proc] : running_) {
    if (proc.handle) {
      CloseHandle(proc.handle);
    }
  }
  running_.clear();
}

std::string GameLauncher::Launch(int64_t game_id,
                                 const std::string& exe_path,
                                 const std::string& working_dir) {
  if (exe_path.empty()) {
    return "Executable path is empty";
  }
  if (IsRunning(game_id)) {
    return "Game is already running";
  }

  const std::wstring exe_w = utf8::Widen(exe_path);
  std::wstring cwd_w = utf8::Widen(working_dir);
  if (cwd_w.empty()) {
    cwd_w = std::filesystem::path(exe_w).parent_path().wstring();
  }

  STARTUPINFOW si{};
  si.cb = sizeof(si);
  PROCESS_INFORMATION pi{};

  std::wstring cmd = L"\"" + exe_w + L"\"";
  std::vector<wchar_t> cmd_buf(cmd.begin(), cmd.end());
  cmd_buf.push_back(L'\0');

  const BOOL ok = CreateProcessW(
      exe_w.c_str(), cmd_buf.data(), nullptr, nullptr, FALSE, 0, nullptr,
      cwd_w.empty() ? nullptr : cwd_w.c_str(), &si, &pi);

  if (!ok) {
    const DWORD err = GetLastError();
    return "CreateProcess failed (" + std::to_string(err) + ")";
  }

  CloseHandle(pi.hThread);

  {
    std::lock_guard lock(mu_);
    running_[game_id] = Proc{pi.hProcess, pi.dwProcessId};
  }
  return {};
}

std::vector<int64_t> GameLauncher::PollExits() {
  std::vector<int64_t> stopped;
  std::lock_guard lock(mu_);
  for (auto it = running_.begin(); it != running_.end();) {
    DWORD code = 0;
    if (GetExitCodeProcess(it->second.handle, &code) && code != STILL_ACTIVE) {
      CloseHandle(it->second.handle);
      stopped.push_back(it->first);
      it = running_.erase(it);
    } else {
      ++it;
    }
  }
  return stopped;
}

std::vector<int64_t> GameLauncher::RunningIds() const {
  std::lock_guard lock(mu_);
  std::vector<int64_t> ids;
  ids.reserve(running_.size());
  for (const auto& [id, proc] : running_) {
    (void)proc;
    ids.push_back(id);
  }
  return ids;
}

bool GameLauncher::IsRunning(int64_t game_id) const {
  std::lock_guard lock(mu_);
  return running_.find(game_id) != running_.end();
}

}  // namespace omni
