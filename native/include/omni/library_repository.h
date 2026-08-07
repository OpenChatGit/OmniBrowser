#pragma once

#include <optional>
#include <string>
#include <vector>

#include "omni/game.h"
#include "sqlite3.h"

namespace omni {

class LibraryRepository {
 public:
  LibraryRepository() = default;
  ~LibraryRepository();

  LibraryRepository(const LibraryRepository&) = delete;
  LibraryRepository& operator=(const LibraryRepository&) = delete;

  bool Open(const std::string& db_path);
  void Close();

  std::vector<Game> List() const;
  std::optional<Game> Get(int64_t id) const;
  std::optional<Game> Add(const Game& game);
  bool Update(const Game& game);
  bool Remove(int64_t id);
  bool TouchPlay(int64_t id, int64_t played_at_unix);

 private:
  bool Migrate();
  Game RowToGame(sqlite3_stmt* stmt) const;

  sqlite3* db_ = nullptr;
};

}  // namespace omni
