#include "omni/library_repository.h"

#include <ctime>

#include "sqlite3.h"

namespace omni {
namespace {

int64_t NowUnix() {
  return static_cast<int64_t>(std::time(nullptr));
}

}  // namespace

LibraryRepository::~LibraryRepository() {
  Close();
}

bool LibraryRepository::Open(const std::string& db_path) {
  Close();
  if (sqlite3_open(db_path.c_str(), &db_) != SQLITE_OK) {
    db_ = nullptr;
    return false;
  }
  sqlite3_exec(db_, "PRAGMA foreign_keys = ON;", nullptr, nullptr, nullptr);
  return Migrate();
}

void LibraryRepository::Close() {
  if (db_) {
    sqlite3_close(db_);
    db_ = nullptr;
  }
}

bool LibraryRepository::Migrate() {
  const char* sql =
      "CREATE TABLE IF NOT EXISTS games ("
      "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
      "  title TEXT NOT NULL,"
      "  exe_path TEXT NOT NULL,"
      "  working_dir TEXT NOT NULL DEFAULT '',"
      "  cover_path TEXT NOT NULL DEFAULT '',"
      "  last_played INTEGER NOT NULL DEFAULT 0,"
      "  playtime_seconds INTEGER NOT NULL DEFAULT 0,"
      "  created_at INTEGER NOT NULL,"
      "  updated_at INTEGER NOT NULL"
      ");";
  char* err = nullptr;
  const int rc = sqlite3_exec(db_, sql, nullptr, nullptr, &err);
  if (rc != SQLITE_OK) {
    if (err) {
      sqlite3_free(err);
    }
    return false;
  }
  return true;
}

Game LibraryRepository::RowToGame(sqlite3_stmt* stmt) const {
  Game g;
  g.id = sqlite3_column_int64(stmt, 0);
  g.title = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
  g.exe_path = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
  g.working_dir = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3));
  g.cover_path = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 4));
  g.last_played = sqlite3_column_int64(stmt, 5);
  g.playtime_seconds = sqlite3_column_int64(stmt, 6);
  g.created_at = sqlite3_column_int64(stmt, 7);
  g.updated_at = sqlite3_column_int64(stmt, 8);
  return g;
}

std::vector<Game> LibraryRepository::List() const {
  std::vector<Game> out;
  if (!db_) {
    return out;
  }
  const char* sql =
      "SELECT id, title, exe_path, working_dir, cover_path, last_played, "
      "playtime_seconds, created_at, updated_at FROM games "
      "ORDER BY last_played DESC, title COLLATE NOCASE ASC;";
  sqlite3_stmt* stmt = nullptr;
  if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
    return out;
  }
  while (sqlite3_step(stmt) == SQLITE_ROW) {
    out.push_back(RowToGame(stmt));
  }
  sqlite3_finalize(stmt);
  return out;
}

std::optional<Game> LibraryRepository::Get(int64_t id) const {
  if (!db_) {
    return std::nullopt;
  }
  const char* sql =
      "SELECT id, title, exe_path, working_dir, cover_path, last_played, "
      "playtime_seconds, created_at, updated_at FROM games WHERE id = ?;";
  sqlite3_stmt* stmt = nullptr;
  if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
    return std::nullopt;
  }
  sqlite3_bind_int64(stmt, 1, id);
  std::optional<Game> result;
  if (sqlite3_step(stmt) == SQLITE_ROW) {
    result = RowToGame(stmt);
  }
  sqlite3_finalize(stmt);
  return result;
}

std::optional<Game> LibraryRepository::Add(const Game& game) {
  if (!db_) {
    return std::nullopt;
  }
  const int64_t now = NowUnix();
  const char* sql =
      "INSERT INTO games (title, exe_path, working_dir, cover_path, "
      "last_played, playtime_seconds, created_at, updated_at) "
      "VALUES (?, ?, ?, ?, 0, 0, ?, ?);";
  sqlite3_stmt* stmt = nullptr;
  if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
    return std::nullopt;
  }
  sqlite3_bind_text(stmt, 1, game.title.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt, 2, game.exe_path.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt, 3, game.working_dir.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt, 4, game.cover_path.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_int64(stmt, 5, now);
  sqlite3_bind_int64(stmt, 6, now);
  if (sqlite3_step(stmt) != SQLITE_DONE) {
    sqlite3_finalize(stmt);
    return std::nullopt;
  }
  const int64_t id = sqlite3_last_insert_rowid(db_);
  sqlite3_finalize(stmt);
  return Get(id);
}

bool LibraryRepository::Update(const Game& game) {
  if (!db_ || game.id <= 0) {
    return false;
  }
  const char* sql =
      "UPDATE games SET title = ?, exe_path = ?, working_dir = ?, "
      "cover_path = ?, updated_at = ? WHERE id = ?;";
  sqlite3_stmt* stmt = nullptr;
  if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
    return false;
  }
  sqlite3_bind_text(stmt, 1, game.title.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt, 2, game.exe_path.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt, 3, game.working_dir.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt, 4, game.cover_path.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_int64(stmt, 5, NowUnix());
  sqlite3_bind_int64(stmt, 6, game.id);
  const bool ok = sqlite3_step(stmt) == SQLITE_DONE;
  sqlite3_finalize(stmt);
  return ok;
}

bool LibraryRepository::Remove(int64_t id) {
  if (!db_) {
    return false;
  }
  const char* sql = "DELETE FROM games WHERE id = ?;";
  sqlite3_stmt* stmt = nullptr;
  if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
    return false;
  }
  sqlite3_bind_int64(stmt, 1, id);
  const bool ok = sqlite3_step(stmt) == SQLITE_DONE;
  sqlite3_finalize(stmt);
  return ok;
}

bool LibraryRepository::TouchPlay(int64_t id, int64_t played_at_unix) {
  if (!db_) {
    return false;
  }
  const char* sql =
      "UPDATE games SET last_played = ?, updated_at = ? WHERE id = ?;";
  sqlite3_stmt* stmt = nullptr;
  if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
    return false;
  }
  sqlite3_bind_int64(stmt, 1, played_at_unix);
  sqlite3_bind_int64(stmt, 2, played_at_unix);
  sqlite3_bind_int64(stmt, 3, id);
  const bool ok = sqlite3_step(stmt) == SQLITE_DONE;
  sqlite3_finalize(stmt);
  return ok;
}

}  // namespace omni
