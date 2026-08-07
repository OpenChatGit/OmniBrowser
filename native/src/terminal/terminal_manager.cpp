#include "omni/terminal_manager.h"

#include <functional>
#include <memory>
#include <vector>

#include "include/cef_parser.h"
#include "include/cef_task.h"
#include "omni/utf8.h"

namespace omni {
namespace {

std::wstring DefaultShell() {
  return L"powershell.exe";
}

std::string ToBase64(const char* data, size_t size) {
  return CefBase64Encode(data, size).ToString();
}

class TerminalOutputTask : public CefTask {
 public:
  explicit TerminalOutputTask(std::function<void()> fn) : fn_(std::move(fn)) {}

  void Execute() override {
    if (fn_) {
      fn_();
    }
  }

 private:
  std::function<void()> fn_;
  IMPLEMENT_REFCOUNTING(TerminalOutputTask);
};

}  // namespace

TerminalManager::TerminalManager() = default;

TerminalManager::~TerminalManager() {
  CloseAll();
}

std::string TerminalManager::Open(const std::string& cwd,
                                  int cols,
                                  int rows,
                                  std::string* error) {
  if (cols < 20) {
    cols = 80;
  }
  if (rows < 5) {
    rows = 24;
  }

  HANDLE pty_in_read = INVALID_HANDLE_VALUE;
  HANDLE pty_in_write = INVALID_HANDLE_VALUE;
  HANDLE pty_out_read = INVALID_HANDLE_VALUE;
  HANDLE pty_out_write = INVALID_HANDLE_VALUE;

  SECURITY_ATTRIBUTES sa{};
  sa.nLength = sizeof(sa);
  sa.bInheritHandle = TRUE;

  if (!CreatePipe(&pty_in_read, &pty_in_write, &sa, 0) ||
      !CreatePipe(&pty_out_read, &pty_out_write, &sa, 0)) {
    if (error) {
      *error = "CreatePipe failed";
    }
    return {};
  }

  SetHandleInformation(pty_in_write, HANDLE_FLAG_INHERIT, 0);
  SetHandleInformation(pty_out_read, HANDLE_FLAG_INHERIT, 0);

  HPCON hpc = nullptr;
  const HRESULT hr = CreatePseudoConsole(
      {static_cast<SHORT>(cols), static_cast<SHORT>(rows)}, pty_in_read,
      pty_out_write, 0, &hpc);
  CloseHandle(pty_in_read);
  CloseHandle(pty_out_write);

  if (FAILED(hr) || !hpc) {
    CloseHandle(pty_in_write);
    CloseHandle(pty_out_read);
    if (error) {
      *error = "CreatePseudoConsole failed";
    }
    return {};
  }

  auto session = std::make_shared<Session>();
  session->hpc = hpc;
  session->pipe_in = pty_in_write;
  session->pipe_out = pty_out_read;
  session->cols = static_cast<SHORT>(cols);
  session->rows = static_cast<SHORT>(rows);

  SIZE_T attr_size = 0;
  InitializeProcThreadAttributeList(nullptr, 1, 0, &attr_size);
  session->attr_list = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(
      HeapAlloc(GetProcessHeap(), 0, attr_size));
  if (!session->attr_list ||
      !InitializeProcThreadAttributeList(session->attr_list, 1, 0, &attr_size) ||
      !UpdateProcThreadAttribute(session->attr_list, 0,
                                 PROC_THREAD_ATTRIBUTE_PSEUDOCONSOLE, hpc,
                                 sizeof(hpc), nullptr, nullptr)) {
    DestroySession(session);
    if (error) {
      *error = "ProcThreadAttribute setup failed";
    }
    return {};
  }

  ZeroMemory(&session->si, sizeof(session->si));
  session->si.StartupInfo.cb = sizeof(STARTUPINFOEXW);
  session->si.lpAttributeList = session->attr_list;

  std::wstring cwd_w = cwd.empty() ? L"" : utf8::Widen(cwd);
  const wchar_t* cwd_ptr = cwd_w.empty() ? nullptr : cwd_w.c_str();

  std::wstring cmd = DefaultShell();
  std::vector<wchar_t> cmdline(cmd.begin(), cmd.end());
  cmdline.push_back(L'\0');

  ZeroMemory(&session->pi, sizeof(session->pi));
  const BOOL ok = CreateProcessW(
      nullptr, cmdline.data(), nullptr, nullptr, FALSE,
      EXTENDED_STARTUPINFO_PRESENT | CREATE_UNICODE_ENVIRONMENT, nullptr,
      cwd_ptr, &session->si.StartupInfo, &session->pi);

  if (!ok) {
    DestroySession(session);
    if (error) {
      *error = "CreateProcessW(powershell) failed";
    }
    return {};
  }

  {
    std::lock_guard<std::mutex> lock(mu_);
    session->id = "t" + std::to_string(next_id_++);
    sessions_[session->id] = session;
  }

  session->reader = std::thread([this, session]() { ReaderLoop(session); });
  return session->id;
}

bool TerminalManager::Write(const std::string& id, const std::string& data) {
  std::shared_ptr<Session> session;
  {
    std::lock_guard<std::mutex> lock(mu_);
    auto it = sessions_.find(id);
    if (it == sessions_.end()) {
      return false;
    }
    session = it->second;
  }
  if (!session->alive || session->pipe_in == INVALID_HANDLE_VALUE ||
      data.empty()) {
    return false;
  }

  const char* bytes = data.data();
  DWORD remaining = static_cast<DWORD>(data.size());
  while (remaining > 0) {
    DWORD written = 0;
    if (!WriteFile(session->pipe_in, bytes, remaining, &written, nullptr)) {
      return false;
    }
    bytes += written;
    remaining -= written;
  }
  return true;
}

bool TerminalManager::Resize(const std::string& id, int cols, int rows) {
  std::shared_ptr<Session> session;
  {
    std::lock_guard<std::mutex> lock(mu_);
    auto it = sessions_.find(id);
    if (it == sessions_.end()) {
      return false;
    }
    session = it->second;
  }
  if (!session->hpc || cols < 20 || rows < 5) {
    return false;
  }
  session->cols = static_cast<SHORT>(cols);
  session->rows = static_cast<SHORT>(rows);
  return SUCCEEDED(
      ResizePseudoConsole(session->hpc, {session->cols, session->rows}));
}

bool TerminalManager::Close(const std::string& id) {
  std::shared_ptr<Session> session;
  {
    std::lock_guard<std::mutex> lock(mu_);
    auto it = sessions_.find(id);
    if (it == sessions_.end()) {
      return false;
    }
    session = it->second;
    sessions_.erase(it);
    if (session->query_id >= 0) {
      query_to_session_.erase(session->query_id);
    }
  }
  // ConPTY teardown (ClosePseudoConsole / reader join) must not run on the
  // CEF UI thread — it can deadlock and freeze the whole app.
  std::thread([session]() { DestroySession(session); }).detach();
  return true;
}

bool TerminalManager::Subscribe(const std::string& id,
                                int64_t query_id,
                                OutputCallback callback) {
  std::lock_guard<std::mutex> lock(mu_);
  auto it = sessions_.find(id);
  if (it == sessions_.end()) {
    return false;
  }
  auto& session = it->second;
  std::lock_guard<std::mutex> cb_lock(session->callback_mu);
  if (session->query_id >= 0) {
    query_to_session_.erase(session->query_id);
  }
  session->callback = callback;
  session->query_id = query_id;
  query_to_session_[query_id] = id;
  return true;
}

void TerminalManager::UnsubscribeByQuery(int64_t query_id) {
  std::lock_guard<std::mutex> lock(mu_);
  auto it = query_to_session_.find(query_id);
  if (it == query_to_session_.end()) {
    return;
  }
  auto sit = sessions_.find(it->second);
  query_to_session_.erase(it);
  if (sit == sessions_.end()) {
    return;
  }
  std::lock_guard<std::mutex> cb_lock(sit->second->callback_mu);
  sit->second->callback = nullptr;
  sit->second->query_id = -1;
}

void TerminalManager::CloseAll() {
  std::vector<std::shared_ptr<Session>> all;
  {
    std::lock_guard<std::mutex> lock(mu_);
    for (auto& entry : sessions_) {
      all.push_back(entry.second);
    }
    sessions_.clear();
    query_to_session_.clear();
  }
  for (auto& session : all) {
    DestroySession(session);
  }
}

void TerminalManager::ReaderLoop(std::shared_ptr<Session> session) {
  char buffer[4096];
  while (session->alive) {
    DWORD read = 0;
    const BOOL ok =
        ReadFile(session->pipe_out, buffer, sizeof(buffer), &read, nullptr);
    if (!ok || read == 0) {
      DeliverOutput(session, {}, true);
      break;
    }
    DeliverOutput(session, std::string(buffer, buffer + read), false);
  }
}

void TerminalManager::DeliverOutput(const std::shared_ptr<Session>& session,
                                    const std::string& chunk,
                                    bool eof) {
  CefPostTask(TID_UI, new TerminalOutputTask([session, chunk, eof]() {
                OutputCallback callback;
                {
                  std::lock_guard<std::mutex> lock(session->callback_mu);
                  callback = session->callback;
                }
                if (!callback) {
                  return;
                }

                std::string json = "{\"id\":\"" + session->id +
                                   "\",\"eof\":" + (eof ? "true" : "false");
                if (!chunk.empty()) {
                  json += ",\"data\":\"" + ToBase64(chunk.data(), chunk.size()) +
                          "\"";
                }
                json += "}";
                callback->Success(json);

                if (eof) {
                  std::lock_guard<std::mutex> lock(session->callback_mu);
                  session->callback = nullptr;
                  session->query_id = -1;
                }
              }));
}

void TerminalManager::DestroySession(const std::shared_ptr<Session>& session) {
  if (!session) {
    return;
  }
  session->alive = false;

  {
    std::lock_guard<std::mutex> lock(session->callback_mu);
    session->callback = nullptr;
    session->query_id = -1;
  }

  // Kill the shell first. Calling ClosePseudoConsole while the process is
  // still alive and a ReadFile is pending is a known ConPTY hang.
  if (session->pi.hProcess) {
    TerminateProcess(session->pi.hProcess, 0);
  }

  // Closing the pipes unblocks the reader thread's ReadFile.
  if (session->pipe_in != INVALID_HANDLE_VALUE) {
    CloseHandle(session->pipe_in);
    session->pipe_in = INVALID_HANDLE_VALUE;
  }
  if (session->pipe_out != INVALID_HANDLE_VALUE) {
    CancelIoEx(session->pipe_out, nullptr);
    CloseHandle(session->pipe_out);
    session->pipe_out = INVALID_HANDLE_VALUE;
  }

  // Join the reader BEFORE ClosePseudoConsole — otherwise ConPTY can deadlock
  // waiting to flush output that the reader never drains.
  if (session->reader.joinable()) {
    if (session->reader.get_id() != std::this_thread::get_id()) {
      session->reader.join();
    } else {
      session->reader.detach();
    }
  }

  if (session->hpc) {
    ClosePseudoConsole(session->hpc);
    session->hpc = nullptr;
  }

  if (session->pi.hProcess) {
    WaitForSingleObject(session->pi.hProcess, 1000);
    CloseHandle(session->pi.hProcess);
    session->pi.hProcess = nullptr;
  }
  if (session->pi.hThread) {
    CloseHandle(session->pi.hThread);
    session->pi.hThread = nullptr;
  }
  if (session->attr_list) {
    DeleteProcThreadAttributeList(session->attr_list);
    HeapFree(GetProcessHeap(), 0, session->attr_list);
    session->attr_list = nullptr;
  }
}

}  // namespace omni
