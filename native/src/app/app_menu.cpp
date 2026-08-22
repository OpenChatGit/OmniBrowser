#include "omni/app_menu.h"

#include "include/base/cef_logging.h"
#include "include/wrapper/cef_helpers.h"
#include "omni/adblock_service.h"
#include "omni/omni_handler.h"
#include "omni/paths.h"

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
};

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

  menu->AddItem(kToggleDevTools, "Dev Tools");
  bind(kToggleDevTools, Json{{"action", "toggle-devtools"}});
  menu->SetAccelerator(kToggleDevTools, 'I', true, true, false);

  menu->AddItem(kOpenInfo, "Info");
  bind(kOpenInfo, Json{{"action", "open-info"}});

  menu->AddItem(kExit, "Exit");
  bind(kExit, Json{{"action", "exit"}});

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
  (void)menu_model;
}

void AppMenuDelegate::MenuClosed(CefRefPtr<CefMenuModel> menu_model) {
  CEF_REQUIRE_UI_THREAD();
  if (!owner_) {
    return;
  }
  if (root_ && menu_model.get() == root_.get()) {
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

}  // namespace omni
