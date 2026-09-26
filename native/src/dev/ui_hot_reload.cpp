#include "omni/ui_hot_reload.h"

#include <atomic>
#include <chrono>
#include <cwctype>
#include <functional>
#include <thread>
#include <vector>

#if defined(_WIN32)
#include <windows.h>
#else
#include <dirent.h>
#include <fcntl.h>
#include <poll.h>
#include <sys/inotify.h>
#include <unistd.h>
#include <cstring>
#endif

#include "include/cef_task.h"
#include "omni/utf8.h"

namespace omni {
namespace {

#if defined(_WIN32)
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
#else
bool IsUiAsset(const std::string& name) {
  const auto dot = name.find_last_of('.');
  if (dot == std::string::npos) {
    return false;
  }
  std::string ext = name.substr(dot);
  for (char& c : ext) {
    if (c >= 'A' && c <= 'Z') {
      c = static_cast<char>(c - 'A' + 'a');
    }
  }
  return ext == ".html" || ext == ".htm" || ext == ".css" || ext == ".js" ||
         ext == ".svg" || ext == ".png" || ext == ".jpg" || ext == ".jpeg" ||
         ext == ".webp" || ext == ".json";
}

void AddWatchesRecursive(int fd, const std::string& dir) {
  if (inotify_add_watch(fd, dir.c_str(),
                        IN_CREATE | IN_DELETE | IN_MODIFY | IN_MOVED_TO |
                            IN_MOVED_FROM | IN_CLOSE_WRITE) < 0) {
    return;
  }
  DIR* d = opendir(dir.c_str());
  if (!d) {
    return;
  }
  while (dirent* ent = readdir(d)) {
    if (ent->d_name[0] == '.') {
      continue;
    }
    if (ent->d_type == DT_DIR || ent->d_type == DT_UNKNOWN) {
      AddWatchesRecursive(fd, dir + "/" + ent->d_name);
    }
  }
  closedir(d);
}
#endif

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
#if defined(_WIN32)
  std::wstring directory;
  HANDLE stop_event = nullptr;
#else
  std::string directory;
  int stop_pipe[2] = {-1, -1};
#endif
  std::function<void()> on_change;
  std::thread worker;
  std::atomic<bool> running{false};
};

UiHotReload::~UiHotReload() {
  Stop();
}

void UiHotReload::Start(const std::string& directory_utf8,
                        std::function<void()> on_change) {
  Stop();
  impl_ = new Impl();
#if defined(_WIN32)
  impl_->directory = utf8::Widen(directory_utf8);
  impl_->stop_event = CreateEventW(nullptr, TRUE, FALSE, nullptr);
#else
  impl_->directory = directory_utf8;
  if (pipe(impl_->stop_pipe) != 0) {
    impl_->stop_pipe[0] = impl_->stop_pipe[1] = -1;
  }
#endif
  impl_->on_change = std::move(on_change);
  impl_->running = true;

  impl_->worker = std::thread([this]() {
#if defined(_WIN32)
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
#else
    const int fd = inotify_init1(IN_NONBLOCK);
    if (fd < 0) {
      return;
    }
    AddWatchesRecursive(fd, impl_->directory);
    std::vector<char> buffer(64 * 1024);
    auto last_fire =
        std::chrono::steady_clock::now() - std::chrono::milliseconds(500);

    while (impl_->running) {
      pollfd fds[2]{};
      fds[0].fd = fd;
      fds[0].events = POLLIN;
      fds[1].fd = impl_->stop_pipe[0];
      fds[1].events = POLLIN;
      const int n = poll(fds, impl_->stop_pipe[0] >= 0 ? 2 : 1, -1);
      if (n <= 0) {
        if (!impl_->running) {
          break;
        }
        continue;
      }
      if (impl_->stop_pipe[0] >= 0 && (fds[1].revents & POLLIN)) {
        break;
      }
      if (!(fds[0].revents & POLLIN)) {
        continue;
      }
      const ssize_t bytes = read(fd, buffer.data(), buffer.size());
      if (bytes <= 0) {
        continue;
      }
      bool relevant = false;
      ssize_t offset = 0;
      while (offset < bytes) {
        auto* event = reinterpret_cast<inotify_event*>(buffer.data() + offset);
        if (event->len > 0 && IsUiAsset(event->name)) {
          relevant = true;
          break;
        }
        offset += static_cast<ssize_t>(sizeof(inotify_event) + event->len);
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
    close(fd);
#endif
  });
}

void UiHotReload::Stop() {
  if (!impl_) {
    return;
  }
  impl_->running = false;
#if defined(_WIN32)
  if (impl_->stop_event) {
    SetEvent(impl_->stop_event);
  }
#else
  if (impl_->stop_pipe[1] >= 0) {
    char x = 1;
    write(impl_->stop_pipe[1], &x, 1);
  }
#endif
  if (impl_->worker.joinable()) {
    impl_->worker.join();
  }
#if defined(_WIN32)
  if (impl_->stop_event) {
    CloseHandle(impl_->stop_event);
  }
#else
  if (impl_->stop_pipe[0] >= 0) {
    close(impl_->stop_pipe[0]);
  }
  if (impl_->stop_pipe[1] >= 0) {
    close(impl_->stop_pipe[1]);
  }
#endif
  delete impl_;
  impl_ = nullptr;
}

}  // namespace omni
