#include "omni/library_commands.h"

#include <ctime>
#include <filesystem>

#include "omni/file_dialog.h"
#include "omni/omni_handler.h"

namespace omni {

bool HandleLibraryCommand(
    OmniHandler* owner,
    const std::string& method,
    const Json& params,
    CefRefPtr<CefMessageRouterBrowserSide::Callback> callback) {
  if (method == "library.list") {
    owner->launcher().PollExits();
    Json arr = Json::array();
    for (const auto& g : owner->repository().List()) {
      arr.push_back(GameToJson(g, owner->launcher().IsRunning(g.id)));
    }
    callback->Success(arr.dump());
    return true;
  }

  if (method == "library.add") {
    Game g;
    g.exe_path = params.value("exe_path", "");
    g.title = params.value("title", "");
    g.working_dir = params.value("working_dir", "");
    g.cover_path = params.value("cover_path", "");
    if (g.exe_path.empty()) {
      callback->Failure(400, "exe_path required");
      return true;
    }
    if (g.title.empty()) {
      g.title = BasenameStem(g.exe_path);
    }
    if (g.working_dir.empty()) {
      g.working_dir = std::filesystem::path(g.exe_path).parent_path().string();
    }
    auto created = owner->repository().Add(g);
    if (!created) {
      callback->Failure(500, "Failed to add game");
      return true;
    }
    callback->Success(GameToJson(*created, false).dump());
    return true;
  }

  if (method == "library.update") {
    Game g;
    g.id = params.value("id", static_cast<int64_t>(0));
    g.title = params.value("title", "");
    g.exe_path = params.value("exe_path", "");
    g.working_dir = params.value("working_dir", "");
    g.cover_path = params.value("cover_path", "");
    if (g.id <= 0 || g.exe_path.empty() || g.title.empty()) {
      callback->Failure(400, "id, title and exe_path required");
      return true;
    }
    if (!owner->repository().Update(g)) {
      callback->Failure(500, "Failed to update game");
      return true;
    }
    auto updated = owner->repository().Get(g.id);
    if (!updated) {
      callback->Failure(500, "Updated game missing");
      return true;
    }
    callback->Success(
        GameToJson(*updated, owner->launcher().IsRunning(g.id)).dump());
    return true;
  }

  if (method == "library.remove") {
    const int64_t id = params.value("id", static_cast<int64_t>(0));
    if (id <= 0) {
      callback->Failure(400, "id required");
      return true;
    }
    if (!owner->repository().Remove(id)) {
      callback->Failure(500, "Failed to remove game");
      return true;
    }
    callback->Success(Json{{"ok", true}}.dump());
    return true;
  }

  if (method == "library.launch") {
    const int64_t id = params.value("id", static_cast<int64_t>(0));
    auto game = owner->repository().Get(id);
    if (!game) {
      callback->Failure(404, "Game not found");
      return true;
    }
    const std::string err =
        owner->launcher().Launch(game->id, game->exe_path, game->working_dir);
    if (!err.empty()) {
      callback->Failure(500, err);
      return true;
    }
    owner->repository().TouchPlay(game->id,
                                  static_cast<int64_t>(std::time(nullptr)));
    callback->Success(Json{{"ok", true}, {"id", game->id}}.dump());
    return true;
  }

  if (method == "library.pickExe") {
    const std::string path = PickExecutableDialog();
    if (path.empty()) {
      callback->Success(Json{{"cancelled", true}}.dump());
      return true;
    }
    callback->Success(
        Json{{"path", path},
             {"suggested_title", BasenameStem(path)},
             {"suggested_working_dir",
              std::filesystem::path(path).parent_path().string()}}
            .dump());
    return true;
  }

  if (method == "library.running") {
    owner->launcher().PollExits();
    callback->Success(Json(owner->launcher().RunningIds()).dump());
    return true;
  }

  return false;
}

}  // namespace omni
