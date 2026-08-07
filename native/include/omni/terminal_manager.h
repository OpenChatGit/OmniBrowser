#pragma once

#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>

#include <windows.h>

#include "include/wrapper/cef_message_router.h"

namespace omni {

class TerminalManager {
 public:
  using OutputCallback = CefRefPtr<CefMessageRouterBrowserSide::Callback>;

  TerminalManager();
  ~TerminalManager();

  TerminalManager(const TerminalManager&) = delete;
  TerminalManager& operator=(const TerminalManager&) = delete;

  std::string Open(const std::string& cwd, int cols, int rows, std::string* error);

  bool Write(const std::string& id, const std::string& data);
  bool Resize(const std::string& id, int cols, int rows);
  bool Close(const std::string& id);

  bool Subscribe(const std::string& id, int64_t query_id, OutputCallback callback);
  void UnsubscribeByQuery(int64_t query_id);
  void CloseAll();

 private:
  struct Session {
    std::string id;
    HPCON hpc = nullptr;
    HANDLE pipe_in = INVALID_HANDLE_VALUE;
    HANDLE pipe_out = INVALID_HANDLE_VALUE;
    PROCESS_INFORMATION pi{};
    STARTUPINFOEXW si{};
    LPPROC_THREAD_ATTRIBUTE_LIST attr_list = nullptr;
    std::thread reader;
    std::atomic<bool> alive{true};
    std::mutex callback_mu;
    OutputCallback callback;
    int64_t query_id = -1;
    SHORT cols = 80;
    SHORT rows = 24;
  };

  void ReaderLoop(std::shared_ptr<Session> session);
  void DeliverOutput(const std::shared_ptr<Session>& session,
                     const std::string& chunk,
                     bool eof);
  static void DestroySession(const std::shared_ptr<Session>& session);

  std::mutex mu_;
  std::unordered_map<std::string, std::shared_ptr<Session>> sessions_;
  std::unordered_map<int64_t, std::string> query_to_session_;
  uint64_t next_id_ = 1;
};

}  // namespace omni
