#pragma once

#include <cstdint>
#include <optional>
#include <string>

namespace omni {

struct Game {
  int64_t id = 0;
  std::string title;
  std::string exe_path;
  std::string working_dir;
  std::string cover_path;
  int64_t last_played = 0;       // unix seconds, 0 = never
  int64_t playtime_seconds = 0;
  int64_t created_at = 0;
  int64_t updated_at = 0;
};

}  // namespace omni
