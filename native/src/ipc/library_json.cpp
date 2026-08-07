#include "omni/library_json.h"

#include <filesystem>

namespace omni {

std::string BasenameStem(const std::string& path) {
  const std::string s = std::filesystem::path(path).stem().string();
  return s.empty() ? "Game" : s;
}

Json GameToJson(const Game& game, bool running) {
  return Json{{"id", game.id},
              {"title", game.title},
              {"exe_path", game.exe_path},
              {"working_dir", game.working_dir},
              {"cover_path", game.cover_path},
              {"last_played", game.last_played},
              {"playtime_seconds", game.playtime_seconds},
              {"created_at", game.created_at},
              {"updated_at", game.updated_at},
              {"running", running}};
}

}  // namespace omni
