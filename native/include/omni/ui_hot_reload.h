#pragma once

#include <functional>
#include <string>

namespace omni {

// Watches a UI directory tree and invokes |on_change| (CEF UI thread) on edits.
class UiHotReload {
 public:
  UiHotReload() = default;
  ~UiHotReload();

  UiHotReload(const UiHotReload&) = delete;
  UiHotReload& operator=(const UiHotReload&) = delete;

  void Start(const std::string& directory_utf8, std::function<void()> on_change);
  void Stop();

 private:
  struct Impl;
  Impl* impl_ = nullptr;
};

}  // namespace omni
