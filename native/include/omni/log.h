#pragma once

#include <windows.h>
#include <chrono>
#include <cstdio>
#include <ctime>
#include <string>

namespace omni {

/** Append a line to %APPDATA%\\OmniBrowser\\omni.log (crashes / lifecycle). */
inline void Log(const std::string& msg) {
  wchar_t appdata[MAX_PATH] = {};
  if (GetEnvironmentVariableW(L"APPDATA", appdata, MAX_PATH) == 0) {
    return;
  }
  std::wstring dir = std::wstring(appdata) + L"\\OmniBrowser";
  CreateDirectoryW(dir.c_str(), nullptr);
  const std::wstring path = dir + L"\\omni.log";

  auto now = std::chrono::system_clock::to_time_t(
      std::chrono::system_clock::now());
  char timebuf[64] = {};
  std::tm tm_now{};
  localtime_s(&tm_now, &now);
  std::strftime(timebuf, sizeof(timebuf), "%Y-%m-%d %H:%M:%S", &tm_now);

  FILE* fp = nullptr;
  if (_wfopen_s(&fp, path.c_str(), L"a") == 0 && fp) {
    std::fprintf(fp, "[%s] [PID %lu] [TID %lu] %s\n", timebuf,
                 ::GetCurrentProcessId(), ::GetCurrentThreadId(),
                 msg.c_str());
    std::fflush(fp);
    std::fclose(fp);
  }
}

/** Overwrite last_crash.txt so the MCP host can report a fresh crash. */
inline void LogCrash(const std::string& msg) {
  Log(msg);
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
}

}  // namespace omni
