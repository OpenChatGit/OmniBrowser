#pragma once

#include <cstdint>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#if defined(_WIN32)
#include <windows.h>
#else
#include <sys/types.h>
#include <unistd.h>
#endif

namespace omni {

class GameLauncher {
 public:
  GameLauncher() = default;
  ~GameLauncher();

  GameLauncher(const GameLauncher&) = delete;
  GameLauncher& operator=(const GameLauncher&) = delete;

  // Starts the game process. Returns empty string on success, else error.
  std::string Launch(int64_t game_id,
                     const std::string& exe_path,
                     const std::string& working_dir);

  // Reaps exited processes; returns game ids that just stopped.
  std::vector<int64_t> PollExits();

  std::vector<int64_t> RunningIds() const;
  bool IsRunning(int64_t game_id) const;

 private:
  struct Proc {
#if defined(_WIN32)
    HANDLE handle = nullptr;
    DWORD pid = 0;
#else
    pid_t pid = -1;
#endif
  };

  mutable std::mutex mu_;
  std::unordered_map<int64_t, Proc> running_;
};

}  // namespace omni
