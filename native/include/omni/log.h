#pragma once

#include <chrono>
#include <cstdio>
#include <ctime>
#include <string>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <cstdlib>
#include <pthread.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#endif

namespace omni {
#if !defined(_WIN32)
namespace {

inline std::string LogDirUtf8() {
  if (const char* xdg = std::getenv("XDG_DATA_HOME"); xdg && xdg[0]) {
    return std::string(xdg) + "/OmniBrowser";
  }
  if (const char* home = std::getenv("HOME"); home && home[0]) {
    return std::string(home) + "/.local/share/OmniBrowser";
  }
  return {};
}

}  // namespace
#endif

/** Append a line to the Omni log (crashes / lifecycle). */
inline void Log(const std::string& msg) {
  auto now = std::chrono::system_clock::to_time_t(
      std::chrono::system_clock::now());
  char timebuf[64] = {};
  std::tm tm_now{};
#if defined(_WIN32)
  localtime_s(&tm_now, &now);
#else
  localtime_r(&now, &tm_now);
#endif
  std::strftime(timebuf, sizeof(timebuf), "%Y-%m-%d %H:%M:%S", &tm_now);

#if defined(_WIN32)
  wchar_t appdata[MAX_PATH] = {};
  if (GetEnvironmentVariableW(L"APPDATA", appdata, MAX_PATH) == 0) {
    return;
  }
  std::wstring dir = std::wstring(appdata) + L"\\OmniBrowser";
  CreateDirectoryW(dir.c_str(), nullptr);
  const std::wstring path = dir + L"\\omni.log";
  FILE* fp = nullptr;
  if (_wfopen_s(&fp, path.c_str(), L"a") == 0 && fp) {
    std::fprintf(fp, "[%s] [PID %lu] [TID %lu] %s\n", timebuf,
                 ::GetCurrentProcessId(), ::GetCurrentThreadId(),
                 msg.c_str());
    std::fflush(fp);
    std::fclose(fp);
  }
#else
  const std::string dir = LogDirUtf8();
  if (dir.empty()) {
    return;
  }
  ::mkdir(dir.c_str(), 0755);
  const std::string path = dir + "/omni.log";
  if (FILE* fp = std::fopen(path.c_str(), "a")) {
    const unsigned long tid =
        static_cast<unsigned long>(::pthread_self());
    std::fprintf(fp, "[%s] [PID %lu] [TID %lu] %s\n", timebuf,
                 static_cast<unsigned long>(::getpid()), tid, msg.c_str());
    std::fflush(fp);
    std::fclose(fp);
  }
#endif
}

/** Overwrite last_crash.txt so the MCP host can report a fresh crash. */
inline void LogCrash(const std::string& msg) {
  Log(msg);
#if defined(_WIN32)
  wchar_t appdata[MAX_PATH] = {};
  if (GetEnvironmentVariableW(L"APPDATA", appdata, MAX_PATH) == 0) {
    return;
  }
  const std::wstring path =
      std::wstring(appdata) + L"\\OmniBrowser\\last_crash.txt";
  FILE* fp = nullptr;
  if (_wfopen_s(&fp, path.c_str(), L"w") == 0 && fp) {
    std::fprintf(fp, "%s\n", msg.c_str());
    std::fflush(fp);
    std::fclose(fp);
  }
#else
  const std::string dir = LogDirUtf8();
  if (dir.empty()) {
    return;
  }
  ::mkdir(dir.c_str(), 0755);
  const std::string path = dir + "/last_crash.txt";
  if (FILE* fp = std::fopen(path.c_str(), "w")) {
    std::fprintf(fp, "%s\n", msg.c_str());
    std::fflush(fp);
    std::fclose(fp);
  }
#endif
}

}  // namespace omni
