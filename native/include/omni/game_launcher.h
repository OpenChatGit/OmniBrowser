#pragma once

#include <cstdint>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include <windows.h>

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
    HANDLE handle = nullptr;
    DWORD pid = 0;
  };

  mutable std::mutex mu_;
  std::unordered_map<int64_t, Proc> running_;
};

}  // namespace omni
