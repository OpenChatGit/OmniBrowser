#include "omni/ui_hot_reload.h"

#include <atomic>
#include <chrono>
#include <cwctype>
#include <thread>
#include <vector>

#include <windows.h>

#include "include/cef_task.h"
#include "omni/utf8.h"

namespace omni {
namespace {

constexpr DWORD kWatchFlags = FILE_NOTIFY_CHANGE_FILE_NAME |
                              FILE_NOTIFY_CHANGE_DIR_NAME |
                              FILE_NOTIFY_CHANGE_LAST_WRITE |
                              FILE_NOTIFY_CHANGE_SIZE;

bool IsUiAsset(const std::wstring& name) {
  const auto dot = name.find_last_of(L'.');
  if (dot == std::wstring::npos) {
    return false;
  }
  std::wstring ext = name.substr(dot);
  for (wchar_t& c : ext) {
    c = static_cast<wchar_t>(towlower(c));
  }
  return ext == L".html" || ext == L".htm" || ext == L".css" || ext == L".js" ||
         ext == L".svg" || ext == L".png" || ext == L".jpg" || ext == L".jpeg" ||
         ext == L".webp" || ext == L".json";
}

class UiReloadTask : public CefTask {
 public:
  explicit UiReloadTask(std::function<void()> fn) : fn_(std::move(fn)) {}

  void Execute() override {
    if (fn_) {
      fn_();
    }
  }

 private:
  std::function<void()> fn_;
  IMPLEMENT_REFCOUNTING(UiReloadTask);
};

}  // namespace

struct UiHotReload::Impl {
  std::wstring directory;
  std::function<void()> on_change;
  std::thread worker;
  HANDLE stop_event = nullptr;
  std::atomic<bool> running{false};
};

UiHotReload::~UiHotReload() {
  Stop();
}

void UiHotReload::Start(const std::string& directory_utf8,
                        std::function<void()> on_change) {
  Stop();
  impl_ = new Impl();
  impl_->directory = utf8::Widen(directory_utf8);
  impl_->on_change = std::move(on_change);
  impl_->stop_event = CreateEventW(nullptr, TRUE, FALSE, nullptr);
  impl_->running = true;

  impl_->worker = std::thread([this]() {
    HANDLE dir = CreateFileW(
        impl_->directory.c_str(), FILE_LIST_DIRECTORY,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
        OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OVERLAPPED,
        nullptr);
    if (dir == INVALID_HANDLE_VALUE) {
      return;
    }

    OVERLAPPED overlapped{};
    overlapped.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    std::vector<BYTE> buffer(64 * 1024);
    auto last_fire = std::chrono::steady_clock::now() -
                     std::chrono::milliseconds(500);

    while (impl_->running) {
      ResetEvent(overlapped.hEvent);
      DWORD bytes = 0;
      const BOOL ok = ReadDirectoryChangesW(
          dir, buffer.data(), static_cast<DWORD>(buffer.size()), TRUE,
          kWatchFlags, &bytes, &overlapped, nullptr);

      if (!ok && GetLastError() != ERROR_IO_PENDING) {
        break;
      }

      HANDLE waits[] = {impl_->stop_event, overlapped.hEvent};
      const DWORD wait = WaitForMultipleObjects(2, waits, FALSE, INFINITE);
      if (wait == WAIT_OBJECT_0) {
        CancelIoEx(dir, &overlapped);
        break;
      }
      if (wait != WAIT_OBJECT_0 + 1) {
        break;
      }

      if (!GetOverlappedResult(dir, &overlapped, &bytes, FALSE) || bytes == 0) {
        continue;
      }

      bool relevant = false;
      auto* info = reinterpret_cast<FILE_NOTIFY_INFORMATION*>(buffer.data());
      for (;;) {
        const std::wstring name(info->FileName,
                                info->FileNameLength / sizeof(WCHAR));
        if (IsUiAsset(name)) {
          relevant = true;
          break;
        }
        if (info->NextEntryOffset == 0) {
          break;
        }
        info = reinterpret_cast<FILE_NOTIFY_INFORMATION*>(
            reinterpret_cast<BYTE*>(info) + info->NextEntryOffset);
      }

      if (!relevant) {
        continue;
      }

      const auto now = std::chrono::steady_clock::now();
      if (now - last_fire < std::chrono::milliseconds(150)) {
        continue;
      }
      last_fire = now;

      CefPostTask(TID_UI, new UiReloadTask(impl_->on_change));
    }

    if (overlapped.hEvent) {
      CloseHandle(overlapped.hEvent);
    }
    CloseHandle(dir);
  });
}

void UiHotReload::Stop() {
  if (!impl_) {
    return;
  }
  impl_->running = false;
  if (impl_->stop_event) {
    SetEvent(impl_->stop_event);
  }
  if (impl_->worker.joinable()) {
    impl_->worker.join();
  }
  if (impl_->stop_event) {
    CloseHandle(impl_->stop_event);
  }
  delete impl_;
  impl_ = nullptr;
}

}  // namespace omni
