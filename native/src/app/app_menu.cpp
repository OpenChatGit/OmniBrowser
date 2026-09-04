#include "omni/app_menu.h"

#if defined(_WIN32)
#include <windows.h>
#include <commctrl.h>
#include <cmath>
#pragma comment(lib, "comctl32.lib")
#endif

#include "include/base/cef_logging.h"
#include "include/wrapper/cef_helpers.h"
#include "omni/adblock_service.h"
#include "omni/appearance.h"
#include "omni/log.h"
#include "omni/omni_handler.h"
#include "omni/paths.h"
#include "omni/settings_store.h"
#include "omni/utf8.h"
#include <vector>

namespace omni {
namespace {

using Json = nlohmann::json;

enum CommandId : int {
  kNewTab = 1001,
  kNewWindow = 1002,
  kNewPrivate = 1003,
  kOpenHistory = 1004,
  kClearData = 1005,
  kExit = 1006,
  kOpenDownloads = 1007,
  kOpenBookmarks = 1008,
  kOpenInfo = 1009,
  kToggleAdblock = 1010,
  kToggleAdblockAggressive = 1011,
  kToggleDevTools = 1012,
  kAppearanceMenu = 1020,
  kAppearanceSystem = 1021,
  kAppearanceDark = 1022,
  kAppearanceLight = 1023,
};

#if defined(_WIN32)
HWND g_root_menu_hwnd = nullptr;
#endif

cef_color_t Argb(int r, int g, int b) {
  return CefColorSetARGB(255, r, g, b);
}

void ApplyMenuColors(CefRefPtr<CefMenuModel> menu) {
  if (!menu) {
    return;
  }
  const bool dark = ChromeShouldUseDark();
  const cef_color_t hover = dark ? Argb(0x38, 0x38, 0x38) : Argb(0xe8, 0xe8, 0xe8);
  const cef_color_t text = dark ? Argb(0xe4, 0xe4, 0xe4) : Argb(0x1a, 0x19, 0x1a);
  const cef_color_t muted = dark ? Argb(0x9e, 0x9e, 0x9e) : Argb(0x75, 0x75, 0x75);

  // 0 removes explicit item background color so items seamlessly match the container background.
  menu->SetColorAt(-1, CEF_MENU_COLOR_BACKGROUND, 0);
  menu->SetColorAt(-1, CEF_MENU_COLOR_BACKGROUND_HOVERED, hover);
  menu->SetColorAt(-1, CEF_MENU_COLOR_TEXT, text);
  menu->SetColorAt(-1, CEF_MENU_COLOR_TEXT_HOVERED, text);
  menu->SetColorAt(-1, CEF_MENU_COLOR_TEXT_ACCELERATOR, muted);
  menu->SetColorAt(-1, CEF_MENU_COLOR_TEXT_ACCELERATOR_HOVERED, muted);
  const size_t count = menu->GetCount();
  for (size_t i = 0; i < count; ++i) {
    const int idx = static_cast<int>(i);
    menu->SetColorAt(idx, CEF_MENU_COLOR_BACKGROUND, 0);
    menu->SetColorAt(idx, CEF_MENU_COLOR_BACKGROUND_HOVERED, hover);
    menu->SetColorAt(idx, CEF_MENU_COLOR_TEXT, text);
    menu->SetColorAt(idx, CEF_MENU_COLOR_TEXT_HOVERED, text);
    menu->SetColorAt(idx, CEF_MENU_COLOR_TEXT_ACCELERATOR, muted);
    menu->SetColorAt(idx, CEF_MENU_COLOR_TEXT_ACCELERATOR_HOVERED, muted);
    if (auto sub = menu->GetSubMenuAt(idx)) {
      ApplyMenuColors(sub);
    }
  }
}

}  // namespace

AppMenuDelegate::AppMenuDelegate(OmniHandler* owner, const Json& payload)
    : owner_(owner), payload_(payload.is_object() ? payload : Json::object()) {}

CefRefPtr<CefMenuModel> AppMenuDelegate::Build() {
  root_ = CefMenuModel::CreateMenuModel(this);
  CefRefPtr<CefMenuModel> menu = root_;
  commands_.clear();

  auto bind = [this](int id, Json command) {
    commands_[id] = std::move(command);
  };

  menu->AddItem(kNewTab, "New Tab");
  bind(kNewTab, Json{{"action", "new-tab"}});
  menu->SetAccelerator(kNewTab, 'T', false, true, false);

  menu->AddItem(kNewWindow, "New Window");
  bind(kNewWindow, Json{{"action", "new-window"}});
  menu->SetAccelerator(kNewWindow, 'N', false, true, false);

  menu->AddItem(kNewPrivate, "New Private Window");
  bind(kNewPrivate, Json{{"action", "new-private"}});
  menu->SetAccelerator(kNewPrivate, 'N', true, true, false);

  menu->AddSeparator();

  menu->AddItem(kOpenHistory, "History");
  bind(kOpenHistory, Json{{"action", "open-history"}});
  menu->SetAccelerator(kOpenHistory, 'H', false, true, false);
  if (paths::IsPrivateMode()) {
    menu->SetEnabled(kOpenHistory, false);
  }

  menu->AddItem(kOpenBookmarks, "Bookmarks");
  bind(kOpenBookmarks, Json{{"action", "open-bookmarks"}});

  menu->AddItem(kOpenDownloads, "Downloads");
  bind(kOpenDownloads, Json{{"action", "open-downloads"}});
  menu->SetAccelerator(kOpenDownloads, 'J', false, true, false);

  menu->AddItem(kClearData, "Delete Browsing Data");
  bind(kClearData, Json{{"action", "clear-data"}});
  if (paths::IsPrivateMode()) {
    menu->SetEnabled(kClearData, false);
  }

  menu->AddSeparator();

  const bool adblock_on = AdblockService::Get().enabled();
  menu->AddCheckItem(kToggleAdblock, "Block ads && trackers");
  menu->SetChecked(kToggleAdblock, adblock_on);
  bind(kToggleAdblock, Json{{"action", "toggle-adblock"}});

  menu->AddCheckItem(kToggleAdblockAggressive, "Aggressive ad blocking");
  menu->SetChecked(kToggleAdblockAggressive,
                   AdblockService::Get().aggressive());
  menu->SetEnabled(kToggleAdblockAggressive, adblock_on);
  bind(kToggleAdblockAggressive, Json{{"action", "toggle-adblock-aggressive"}});

  menu->AddSeparator();

  CefRefPtr<CefMenuModel> appearance = menu->AddSubMenu(kAppearanceMenu, "Appearance");
  appearance->AddRadioItem(kAppearanceSystem, "System", 1);
  appearance->AddRadioItem(kAppearanceDark, "Dark", 1);
  appearance->AddRadioItem(kAppearanceLight, "Light", 1);
  const std::string pref = AppearancePreference();
  appearance->SetChecked(kAppearanceSystem, pref == "system");
  appearance->SetChecked(kAppearanceDark, pref == "dark");
  appearance->SetChecked(kAppearanceLight, pref == "light");

  menu->AddSeparator();

  menu->AddItem(kToggleDevTools, "Dev Tools");
  bind(kToggleDevTools, Json{{"action", "toggle-devtools"}});
  menu->SetAccelerator(kToggleDevTools, 'I', true, true, false);

  menu->AddItem(kOpenInfo, "Info");
  bind(kOpenInfo, Json{{"action", "open-info"}});

  menu->AddItem(kExit, "Exit");
  bind(kExit, Json{{"action", "exit"}});

  ApplyMenuColors(menu);
  return menu;
}

void AppMenuDelegate::ExecuteCommand(CefRefPtr<CefMenuModel> menu_model,
                                     int command_id,
                                     cef_event_flags_t event_flags) {
  CEF_REQUIRE_UI_THREAD();
  (void)menu_model;
  (void)event_flags;
  if (command_id == kToggleAdblock) {
    auto& adblock = AdblockService::Get();
    adblock.set_enabled(!adblock.enabled());
    Emit(Json{{"action", "adblock-changed"},
              {"enabled", adblock.enabled()},
              {"aggressive", adblock.aggressive()}});
    return;
  }
  if (command_id == kToggleAdblockAggressive) {
    auto& adblock = AdblockService::Get();
    adblock.set_aggressive(!adblock.aggressive());
    // Aggressive mode swaps which lists are loaded.
    adblock.ReloadEngine();
    Emit(Json{{"action", "adblock-changed"},
              {"enabled", adblock.enabled()},
              {"aggressive", adblock.aggressive()}});
    return;
  }
  if (command_id == kAppearanceSystem || command_id == kAppearanceDark ||
      command_id == kAppearanceLight) {
    const char* value = "system";
    if (command_id == kAppearanceDark) {
      value = "dark";
    } else if (command_id == kAppearanceLight) {
      value = "light";
    }
    settings::Set("appearance", value);
    ApplyChromeAppearance();
    return;
  }
  if (command_id == kToggleDevTools) {
    if (owner_) {
      owner_->ToggleDevTools();
    }
    return;
  }
  if (paths::IsPrivateMode() &&
      (command_id == kOpenHistory || command_id == kClearData)) {
    return;
  }
  auto it = commands_.find(command_id);
  if (it == commands_.end()) {
    return;
  }
  Emit(it->second);
}

void AppMenuDelegate::MenuWillShow(CefRefPtr<CefMenuModel> menu_model) {
  CEF_REQUIRE_UI_THREAD();
  if (!menu_model) {
    return;
  }
  ApplyMenuColors(menu_model);
  const std::string pref = AppearancePreference();
  if (menu_model->GetIndexOf(kAppearanceSystem) >= 0) {
    menu_model->SetChecked(kAppearanceSystem, pref == "system");
    menu_model->SetChecked(kAppearanceDark, pref == "dark");
    menu_model->SetChecked(kAppearanceLight, pref == "light");
  }
}

void AppMenuDelegate::MenuClosed(CefRefPtr<CefMenuModel> menu_model) {
  CEF_REQUIRE_UI_THREAD();
  if (!owner_) {
    return;
  }
  bool should_close = true;
  if (menu_model && menu_model->IsSubMenu()) {
#if defined(_WIN32)
    HWND root = g_root_menu_hwnd;
    if (root && IsWindow(root) && IsWindowVisible(root)) {
      should_close = false;
    }
#endif
  }
  if (should_close) {
    owner_->HideHistoryFlyout();
    owner_->OnAppMenuClosed();
  }
}

void AppMenuDelegate::Emit(const Json& command) {
  if (!owner_) {
    return;
  }
  owner_->EmitMenuCommand(command);
}

#if defined(_WIN32)
namespace {

HWINEVENTHOOK g_submenu_hook = nullptr;
HHOOK g_menu_cbt = nullptr;
HWND g_main_hwnd = nullptr;
HWND g_pending_submenu = nullptr;
UINT_PTR g_submenu_timer = 0;
UINT_PTR g_menu_poll_timer = 0;

bool IsMenuPopup(HWND hwnd) {
  if (!hwnd || !IsWindow(hwnd) || hwnd == g_main_hwnd) {
    return false;
  }
  wchar_t cls[64] = {};
  if (GetClassNameW(hwnd, cls, 64) <= 0) {
    return false;
  }
  if (wcscmp(cls, L"OmniAiHud") == 0 || wcscmp(cls, L"tooltips_class32") == 0) {
    return false;
  }
  LONG_PTR style = GetWindowLongPtrW(hwnd, GWL_STYLE);
  if (!(style & WS_POPUP)) {
    return false;
  }
  RECT rc = {};
  GetWindowRect(hwnd, &rc);
  if ((rc.right - rc.left) < 30 || (rc.bottom - rc.top) < 30) {
    return false;
  }
  return true;
}

LRESULT CALLBACK SubmenuSubclassProc(HWND hwnd,
                                     UINT msg,
                                     WPARAM wparam,
                                     LPARAM lparam,
                                     UINT_PTR subclass_id,
                                     DWORD_PTR ref_data) {
  if (msg == WM_WINDOWPOSCHANGING) {
    auto* pos = reinterpret_cast<WINDOWPOS*>(lparam);
    if (pos) {
      HWND root = g_root_menu_hwnd;
      if (root && IsWindow(root) && hwnd != root) {
        RECT root_rc = {};
        if (GetWindowRect(root, &root_rc)) {
          if (pos->x >= root_rc.right - 32) {
            int width = pos->cx > 0 ? pos->cx : static_cast<int>(ref_data);
            if (width <= 0) {
              RECT rc = {};
              GetWindowRect(hwnd, &rc);
              width = rc.right - rc.left;
            }
            if (width <= 0) {
              width = 150;
            }
            int target_x = root_rc.left - width + 4;
            HMONITOR mon = MonitorFromWindow(root, MONITOR_DEFAULTTONEAREST);
            MONITORINFO mi = {};
            mi.cbSize = sizeof(mi);
            if (GetMonitorInfoW(mon, &mi) && target_x < mi.rcWork.left) {
              target_x = mi.rcWork.left;
            }
            pos->x = target_x;
            pos->flags &= ~SWP_NOMOVE;
          }
        }
      }
    }
  } else if (msg == WM_NCDESTROY) {
    RemoveWindowSubclass(hwnd, SubmenuSubclassProc, subclass_id);
  }
  return DefSubclassProc(hwnd, msg, wparam, lparam);
}

void RepositionSubmenuToLeft(HWND hwnd) {
  if (!hwnd || !IsWindow(hwnd)) {
    return;
  }
  HWND root = g_root_menu_hwnd;
  if (!root || root == hwnd || !IsWindow(root)) {
    return;
  }
  RECT child_rc = {};
  RECT root_rc = {};
  if (!GetWindowRect(hwnd, &child_rc) || !GetWindowRect(root, &root_rc)) {
    return;
  }
  const int width = child_rc.right - child_rc.left;
  if (width <= 0) {
    return;
  }
  // If the submenu is on the right side of the parent menu, flip it inward to the left.
  if (child_rc.left >= root_rc.right - 32) {
    int target_x = root_rc.left - width + 4;
    HMONITOR mon = MonitorFromWindow(root, MONITOR_DEFAULTTONEAREST);
    MONITORINFO mi = {};
    mi.cbSize = sizeof(mi);
    if (GetMonitorInfoW(mon, &mi)) {
      if (target_x < mi.rcWork.left) {
        target_x = mi.rcWork.left;
      }
    }
    omni::Log("RepositionSubmenuToLeft: moving hwnd=" +
              std::to_string(reinterpret_cast<uintptr_t>(hwnd)) +
              " from x=" + std::to_string(child_rc.left) +
              " to left target_x=" + std::to_string(target_x));
    SetWindowPos(hwnd, nullptr, target_x, child_rc.top, 0, 0,
                 SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
    SetWindowSubclass(hwnd, SubmenuSubclassProc, 1001, static_cast<DWORD_PTR>(width));
  }
}

VOID CALLBACK MenuPollTimerProc(HWND, UINT, UINT_PTR, DWORD) {
  struct PopupInfo {
    HWND hwnd;
    RECT rc;
    std::wstring cls;
  };
  std::vector<PopupInfo> popups;

  EnumThreadWindows(
      GetCurrentThreadId(),
      [](HWND h, LPARAM lp) -> BOOL {
        auto* list = reinterpret_cast<std::vector<PopupInfo>*>(lp);
        if (IsMenuPopup(h)) {
          RECT rc = {};
          GetWindowRect(h, &rc);
          wchar_t cls[64] = {};
          GetClassNameW(h, cls, 64);
          list->push_back({h, rc, cls});
        }
        return TRUE;
      },
      reinterpret_cast<LPARAM>(&popups));

  if (popups.empty()) {
    if (g_root_menu_hwnd != nullptr) {
      g_root_menu_hwnd = nullptr;
      auto* handler = OmniHandler::GetInstance();
      if (handler && handler->app_menu_open()) {
        handler->OnAppMenuClosed();
      }
    }
    return;
  }

  HWND root = g_root_menu_hwnd;
  if (!root || !IsWindow(root)) {
    if (popups.size() == 1) {
      g_root_menu_hwnd = popups[0].hwnd;
      root = g_root_menu_hwnd;
    } else if (popups.size() >= 2) {
      // Pick the taller one as the main root menu
      if ((popups[0].rc.bottom - popups[0].rc.top) >=
          (popups[1].rc.bottom - popups[1].rc.top)) {
        g_root_menu_hwnd = popups[0].hwnd;
      } else {
        g_root_menu_hwnd = popups[1].hwnd;
      }
      root = g_root_menu_hwnd;
    }
  }

  if (!root || !IsWindow(root)) {
    return;
  }

  RECT root_rc = {};
  if (!GetWindowRect(root, &root_rc)) {
    return;
  }

  for (const auto& item : popups) {
    if (item.hwnd != root) {
      if (item.rc.left >= root_rc.right - 32) {
        int width = item.rc.right - item.rc.left;
        int target_x = root_rc.left - width + 4;
        HMONITOR mon = MonitorFromWindow(root, MONITOR_DEFAULTTONEAREST);
        MONITORINFO mi = {};
        mi.cbSize = sizeof(mi);
        if (GetMonitorInfoW(mon, &mi)) {
          if (target_x < mi.rcWork.left) {
            target_x = mi.rcWork.left;
          }
        }
        omni::Log("MenuPollTimer: flipping submenu hwnd=" +
                  std::to_string(reinterpret_cast<uintptr_t>(item.hwnd)) +
                  " from x=" + std::to_string(item.rc.left) +
                  " to left target_x=" + std::to_string(target_x));
        SetWindowPos(item.hwnd, nullptr, target_x, item.rc.top, 0, 0,
                     SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
        SetWindowSubclass(item.hwnd, SubmenuSubclassProc, 1001, static_cast<DWORD_PTR>(width));
      }
    }
  }
}

VOID CALLBACK SubmenuFlipTimer(HWND, UINT, UINT_PTR id, DWORD) {
  KillTimer(nullptr, id);
  if (g_submenu_timer == id) {
    g_submenu_timer = 0;
  }
  if (g_pending_submenu) {
    RepositionSubmenuToLeft(g_pending_submenu);
    g_pending_submenu = nullptr;
  }
}

LRESULT CALLBACK MenuCbtProc(int code, WPARAM wParam, LPARAM lParam) {
  if (code == HCBT_CREATEWND) {
    HWND hwnd = reinterpret_cast<HWND>(wParam);
    auto* created = reinterpret_cast<CBT_CREATEWNDW*>(lParam);
    const wchar_t* cls = nullptr;
    wchar_t name[64] = {};
    if (created && created->lpcs && created->lpcs->lpszClass &&
        !IS_INTRESOURCE(created->lpcs->lpszClass)) {
      cls = created->lpcs->lpszClass;
    } else if (hwnd && GetClassNameW(hwnd, name, 64) > 0) {
      cls = name;
    }
    bool is_menu_class = false;
    if (cls) {
      if (wcscmp(cls, L"#32768") == 0 ||
          wcsncmp(cls, L"Chrome_WidgetWin", 16) == 0) {
        is_menu_class = true;
      }
    }
    if (is_menu_class && hwnd != g_main_hwnd) {
      DWORD style = (created && created->lpcs) ? created->lpcs->style : 0;
      bool is_popup = (style & WS_POPUP) && !(style & WS_CAPTION) && !(style & WS_CHILD);
      if (!is_popup && hwnd) {
        DWORD cur_style = static_cast<DWORD>(GetWindowLongPtrW(hwnd, GWL_STYLE));
        is_popup = (cur_style & WS_POPUP) && !(cur_style & WS_CAPTION) && !(cur_style & WS_CHILD);
      }
      if (is_popup) {
        if (cls && wcscmp(cls, L"#32768") == 0) {
          ApplyNativeMenuAppearance(hwnd);
        }
        if (!g_root_menu_hwnd || !IsWindow(g_root_menu_hwnd)) {
          g_root_menu_hwnd = hwnd;
        } else if (hwnd != g_root_menu_hwnd) {
          int init_width = (created && created->lpcs) ? created->lpcs->cx : 0;
          SetWindowSubclass(hwnd, SubmenuSubclassProc, 1001, static_cast<DWORD_PTR>(init_width));
        }
      }
    }
  }
  return CallNextHookEx(g_menu_cbt, code, wParam, lParam);
}

void CALLBACK SubmenuWinEvent(HWINEVENTHOOK,
                              DWORD event,
                              HWND hwnd,
                              LONG id_object,
                              LONG,
                              DWORD,
                              DWORD) {
  if (event != EVENT_OBJECT_SHOW || !hwnd) {
    return;
  }
  if (!IsMenuPopup(hwnd)) {
    return;
  }
  wchar_t cls[64] = {};
  if (GetClassNameW(hwnd, cls, 64) > 0 && wcscmp(cls, L"#32768") == 0) {
    ApplyNativeMenuAppearance(hwnd);
  }
  if (!g_root_menu_hwnd || !IsWindow(g_root_menu_hwnd)) {
    g_root_menu_hwnd = hwnd;
    return;
  }
  if (hwnd == g_root_menu_hwnd) {
    return;
  }
  SetWindowSubclass(hwnd, SubmenuSubclassProc, 1001, 0);
  RepositionSubmenuToLeft(hwnd);
  g_pending_submenu = hwnd;
  if (g_submenu_timer) {
    KillTimer(nullptr, g_submenu_timer);
  }
  g_submenu_timer = SetTimer(nullptr, 0, 16, SubmenuFlipTimer);
}

}  // namespace

void InstallAppMenuSubmenuHook(HWND main_hwnd) {
  g_main_hwnd = main_hwnd;
  g_root_menu_hwnd = nullptr;
  g_pending_submenu = nullptr;
  if (!g_menu_poll_timer) {
    g_menu_poll_timer = SetTimer(nullptr, 0, 15, MenuPollTimerProc);
  }
  if (!g_submenu_hook) {
    g_submenu_hook = SetWinEventHook(
        EVENT_OBJECT_SHOW, EVENT_OBJECT_SHOW, nullptr, SubmenuWinEvent,
        GetCurrentProcessId(), 0, WINEVENT_OUTOFCONTEXT);
  }
  if (!g_menu_cbt) {
    g_menu_cbt = SetWindowsHookExW(WH_CBT, MenuCbtProc, nullptr,
                                   GetCurrentThreadId());
  }
}

void RemoveAppMenuSubmenuHook() {
  if (g_menu_poll_timer) {
    KillTimer(nullptr, g_menu_poll_timer);
    g_menu_poll_timer = 0;
  }
  if (g_submenu_timer) {
    KillTimer(nullptr, g_submenu_timer);
    g_submenu_timer = 0;
  }
  g_pending_submenu = nullptr;
  g_root_menu_hwnd = nullptr;
  g_main_hwnd = nullptr;
  if (g_menu_cbt) {
    UnhookWindowsHookEx(g_menu_cbt);
    g_menu_cbt = nullptr;
  }
  if (g_submenu_hook) {
    UnhookWinEvent(g_submenu_hook);
    g_submenu_hook = nullptr;
  }
}
#endif

}  // namespace omni
