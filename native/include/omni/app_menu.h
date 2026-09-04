#pragma once

#include <map>

#include "include/cef_menu_model.h"
#include "include/cef_menu_model_delegate.h"
#include "omni/json.hpp"

namespace omni {

class OmniHandler;

// Native Views app menu (Chrome/Brave MenuRunner path).
class AppMenuDelegate : public CefMenuModelDelegate {
 public:
  AppMenuDelegate(OmniHandler* owner, const nlohmann::json& payload);
  ~AppMenuDelegate() override = default;

  CefRefPtr<CefMenuModel> Build();

  void ExecuteCommand(CefRefPtr<CefMenuModel> menu_model,
                      int command_id,
                      cef_event_flags_t event_flags) override;
  void MenuWillShow(CefRefPtr<CefMenuModel> menu_model) override;
  void MenuClosed(CefRefPtr<CefMenuModel> menu_model) override;

 private:
  void Emit(const nlohmann::json& command);

  OmniHandler* owner_;
  nlohmann::json payload_;
  std::map<int, nlohmann::json> commands_;
  CefRefPtr<CefMenuModel> root_;

  IMPLEMENT_REFCOUNTING(AppMenuDelegate);
};

#if defined(_WIN32)
#include <windows.h>
void InstallAppMenuSubmenuHook(HWND main_hwnd = nullptr);
void RemoveAppMenuSubmenuHook();
#endif

}  // namespace omni
