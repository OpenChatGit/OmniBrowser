#pragma once

#include <string>

namespace omni::paths {

std::string ExecutableDir();
std::string AppDataDir();
std::string EnsureAppDataDir();
std::string DatabasePath();

// Persistent visit history (JSON array of {url,title,ts}).
std::string HistoryPath();

// Persistent bookmarks (JSON array of {url,title,ts}).
std::string BookmarksPath();

// Persistent download list (JSON array).
std::string DownloadsPath();

// Optional tab payload when spawning a new window (window.newWithTab).
std::string PendingOpenTabPath();

// User Downloads folder (or AppData\Downloads fallback).
std::string UserDownloadsDir();
std::string EnsureUserDownloadsDir();

// Optional secondary process id (from --omni-instance=...). Empty = primary profile.
void SetProfileInstanceId(const std::string& id);
std::string ProfileInstanceId();

// Private window: isolated CEF profile, no visit history, wiped on exit.
void SetPrivateMode(bool on);
bool IsPrivateMode();
void WipePrivateProfile();

// CEF profile root (cookies, localStorage, history disk state).
std::string CacheRootDir();
std::string EnsureCacheRootDir();

// Directory that contains index.html (repo ui/ in dev, exe/ui in release).
std::string UiRootDir();

// Adblock-rust filter lists / prefs under AppData and bundled defaults.
std::string AdblockDir();
std::string EnsureAdblockDir();
std::string AdblockPrefsPath();
std::string BundledAdblockDir();

// file:/// URL to index.html
std::string UiEntryUrl();

// file:/// URL to overlay.html (dropdown overlay browser).
std::string UiOverlayUrl();

std::string PathToFileUrl(const std::string& path);

}  // namespace omni::paths
