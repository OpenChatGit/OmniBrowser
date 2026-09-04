#include "omni/appearance.h"

#include "omni/omni_handler.h"
#include "omni/paths.h"
#include "omni/settings_store.h"

#if defined(_WIN32)
#include <commctrl.h>
#include <dwmapi.h>
#include <uxtheme.h>
#include <windows.h>
#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "uxtheme.lib")
#ifndef DWMWA_USE_IMMERSIVE_DARK_MODE
#define DWMWA_USE_IMMERSIVE_DARK_MODE 20
#endif
#ifndef MN_GETHMENU
#define MN_GETHMENU 0x01E1
#endif
#endif

#include "include/cef_request_context.h"
#include "include/views/cef_browser_view.h"
#include "include/views/cef_window.h"
#include "omni/library_json.h"

namespace omni {

std::string AppearancePreference() {
  const Json value = settings::Get("appearance");
  if (value.is_string()) {
    const std::string pref = value.get<std::string>();
    if (pref == "light" || pref == "dark" || pref == "system") {
      return pref;
    }
  }
  return "system";
}

bool SystemAppsUseDark() {
#if defined(_WIN32)
  DWORD light = 1;
  DWORD size = sizeof(light);
  const LONG err = RegGetValueW(
      HKEY_CURRENT_USER,
      L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
      L"AppsUseLightTheme", RRF_RT_REG_DWORD, nullptr, &light, &size);
  if (err != ERROR_SUCCESS) {
    return true;
  }
  return light == 0;
#else
  return true;
#endif
}

bool ChromeShouldUseDark() {
  if (paths::IsPrivateMode()) {
    return true;
  }
  const std::string pref = AppearancePreference();
  if (pref == "light") {
    return false;
  }
  if (pref == "dark") {
    return true;
  }
  return SystemAppsUseDark();
}

#if defined(_WIN32)
namespace {

HMODULE UxThemeModule() {
  HMODULE uxtheme = GetModuleHandleW(L"uxtheme.dll");
  if (!uxtheme) {
    uxtheme = LoadLibraryW(L"uxtheme.dll");
  }
  return uxtheme;
}

HBRUSH MenuBackgroundBrush() {
  static HBRUSH light = CreateSolidBrush(RGB(0xff, 0xff, 0xff));
  static HBRUSH dark = CreateSolidBrush(RGB(0x1a, 0x19, 0x1a));
  return ChromeShouldUseDark() ? dark : light;
}

LRESULT CALLBACK MenuWindowSubclassProc(HWND hwnd,
                                        UINT msg,
                                        WPARAM wparam,
                                        LPARAM lparam,
                                        UINT_PTR subclass_id,
                                        DWORD_PTR) {
  if (msg == WM_ERASEBKGND) {
    HDC hdc = reinterpret_cast<HDC>(wparam);
    if (hdc) {
      RECT rc = {};
      GetClientRect(hwnd, &rc);
      FillRect(hdc, &rc, MenuBackgroundBrush());
      return 1;
    }
  } else if (msg == WM_NCDESTROY) {
    RemoveWindowSubclass(hwnd, MenuWindowSubclassProc, subclass_id);
  }
  return DefSubclassProc(hwnd, msg, wparam, lparam);
}

}  // namespace

void ApplyNativeWindowAppearance(HWND hwnd) {
  if (!hwnd) {
    return;
  }
  BOOL dark = ChromeShouldUseDark() ? TRUE : FALSE;
  DwmSetWindowAttribute(hwnd, DWMWA_USE_IMMERSIVE_DARK_MODE, &dark,
                        sizeof(dark));
  DwmSetWindowAttribute(hwnd, 19, &dark, sizeof(dark));
}

void ApplyImmersiveMenuAppearance() {
  HMODULE uxtheme = UxThemeModule();
  if (!uxtheme) {
    return;
  }
  using SetPreferredAppModeFn = int(WINAPI*)(int);
  using FlushMenuThemesFn = void(WINAPI*)();
  auto set_mode = reinterpret_cast<SetPreferredAppModeFn>(
      GetProcAddress(uxtheme, MAKEINTRESOURCEA(135)));
  auto flush = reinterpret_cast<FlushMenuThemesFn>(
      GetProcAddress(uxtheme, MAKEINTRESOURCEA(136)));
  if (set_mode) {
    // 2 = ForceDark, 3 = ForceLight (uxtheme ordinal 135, Win10 1903+).
    set_mode(ChromeShouldUseDark() ? 2 : 3);
  }
  if (flush) {
    flush();
  }
}

void ApplyNativeMenuAppearance(HWND hwnd) {
  if (!hwnd) {
    return;
  }
  const bool dark = ChromeShouldUseDark();
  ApplyNativeWindowAppearance(hwnd);

  HMODULE uxtheme = UxThemeModule();
  if (uxtheme) {
    using AllowDarkModeForWindowFn = BOOL(WINAPI*)(HWND, BOOL);
    auto allow = reinterpret_cast<AllowDarkModeForWindowFn>(
        GetProcAddress(uxtheme, MAKEINTRESOURCEA(133)));
    if (allow) {
      allow(hwnd, dark ? TRUE : FALSE);
    }
  }
  SetWindowTheme(hwnd, dark ? L"DarkMode_ImmersiveStart" : L"",
                 dark ? nullptr : L"");

  HMENU menu = reinterpret_cast<HMENU>(SendMessageW(hwnd, MN_GETHMENU, 0, 0));
  if (menu) {
    MENUINFO info{};
    info.cbSize = sizeof(info);
    info.fMask = MIM_BACKGROUND | MIM_APPLYTOSUBMENUS;
    info.hbrBack = MenuBackgroundBrush();
    SetMenuInfo(menu, &info);
  }
  SetWindowSubclass(hwnd, MenuWindowSubclassProc, 42, 0);
  RedrawWindow(hwnd, nullptr, nullptr,
               RDW_INVALIDATE | RDW_ERASE | RDW_FRAME | RDW_ALLCHILDREN);
}

namespace {

LRESULT CALLBACK AppearanceSubclassProc(HWND hwnd,
                                        UINT msg,
                                        WPARAM wparam,
                                        LPARAM lparam,
                                        UINT_PTR subclass_id,
                                        DWORD_PTR) {
  (void)wparam;
  if (msg == WM_SETTINGCHANGE) {
    const wchar_t* name =
        lparam ? reinterpret_cast<const wchar_t*>(lparam) : L"";
    if (name && wcscmp(name, L"ImmersiveColorSet") == 0) {
      if (AppearancePreference() == "system") {
        ApplyChromeAppearance();
      }
    }
  } else if (msg == WM_THEMECHANGED) {
    if (AppearancePreference() == "system") {
      ApplyChromeAppearance();
    }
  } else if (msg == WM_NCDESTROY) {
    RemoveWindowSubclass(hwnd, AppearanceSubclassProc, subclass_id);
  }
  return DefSubclassProc(hwnd, msg, wparam, lparam);
}

}  // namespace

void InstallSystemAppearanceListener(HWND hwnd) {
  if (!hwnd) {
    return;
  }
  HWND root = GetAncestor(hwnd, GA_ROOT);
  if (root) {
    hwnd = root;
  }
  SetWindowSubclass(hwnd, AppearanceSubclassProc, 41, 0);
}
#endif

void ApplyChromeAppearance() {
  auto* handler = OmniHandler::GetInstance();
  if (!handler) {
    return;
  }
#if defined(_WIN32)
  if (auto shell = handler->shell_browser_view()) {
    if (auto window = shell->GetWindow()) {
      ApplyNativeWindowAppearance(window->GetWindowHandle());
    }
  }
  ApplyImmersiveMenuAppearance();
#endif
  auto context = CefRequestContext::GetGlobalContext();
  if (context) {
    cef_color_variant_t variant = CEF_COLOR_VARIANT_SYSTEM;
    const std::string pref = AppearancePreference();
    if (pref == "dark") {
      variant = CEF_COLOR_VARIANT_DARK;
    } else if (pref == "light") {
      variant = CEF_COLOR_VARIANT_LIGHT;
    }
    context->SetChromeColorScheme(variant, 0);
  }
  handler->NotifyAppearanceChanged();
}

}  // namespace omni
