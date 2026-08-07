#pragma once

#include "include/views/cef_menu_button.h"
#include "include/views/cef_menu_button_delegate.h"

namespace omni {

// Invisible host so ShowMenu runs through CefMenuButton (HAS_MNEMONICS),
// matching Chrome/Brave AppMenu — not Window::ShowMenu (CONTEXT_MENU), which
// flips open to the right at the window edge on Windows.
class AppMenuButtonHost : public CefMenuButtonDelegate {
 public:
  AppMenuButtonHost() = default;

  void OnMenuButtonPressed(
      CefRefPtr<CefMenuButton> menu_button,
      const CefPoint& screen_point,
      CefRefPtr<CefMenuButtonPressedLock> button_pressed_lock) override {
    (void)menu_button;
    (void)screen_point;
    (void)button_pressed_lock;
  }

  void OnButtonPressed(CefRefPtr<CefButton> button) override { (void)button; }

 private:
  IMPLEMENT_REFCOUNTING(AppMenuButtonHost);
};

}  // namespace omni
