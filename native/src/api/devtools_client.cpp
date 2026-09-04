#include "omni/devtools_client.h"

#include <chrono>
#include <map>
#include <mutex>
#include <thread>

#include "include/base/cef_bind.h"
#include "include/base/cef_callback.h"
#include "include/cef_registration.h"
#include "include/wrapper/cef_closure_task.h"

namespace omni {
namespace {

struct PendingCall {
  DevToolsCallback callback;
};

struct BrowserSession {
  CefRefPtr<CefRegistration> registration;
  bool intercept_on = false;
  Json file_chooser;
};

std::mutex g_mu;
int g_next_id = 100;
std::map<int, PendingCall> g_pending;
std::map<int, BrowserSession> g_sessions;

Json ParseMessage(const void* message, size_t size) {
  if (!message || size == 0) {
    return Json();
  }
  const std::string raw(static_cast<const char*>(message), size);
  return Json::parse(raw, nullptr, false);
}

}  // namespace

DevToolsClient& DevToolsClient::Get() {
  static CefRefPtr<DevToolsClient> inst = new DevToolsClient();
  return *inst;
}

void DevToolsClient::Attach(CefRefPtr<CefBrowser> browser) {
  CEF_REQUIRE_UI_THREAD();
  if (!browser) {
    return;
  }
  auto host = browser->GetHost();
  if (!host) {
    return;
  }
  const int bid = browser->GetIdentifier();
  bool need_intercept = false;
  {
    std::lock_guard<std::mutex> lock(g_mu);
    auto& session = g_sessions[bid];
    if (!session.registration) {
      session.registration = host->AddDevToolsMessageObserver(this);
    }
    if (!session.intercept_on) {
      session.intercept_on = true;
      need_intercept = true;
    }
  }
  if (!need_intercept) {
    return;
  }
  // Enable domains and swallow the OS file dialog so agents can set files
  // via DOM.setFileInputFiles after clicking an upload button.
  Call(browser, "Runtime.enable", Json::object(), {});
  Call(browser, "Page.enable", Json::object(), {});
  Call(browser, "DOM.enable", Json::object(), {});
  Call(browser, "Page.setInterceptFileChooserDialog",
       Json{{"enabled", true}}, {});
}

void DevToolsClient::Call(CefRefPtr<CefBrowser> browser,
                          const std::string& method,
                          const Json& params,
                          DevToolsCallback callback) {
  CEF_REQUIRE_UI_THREAD();
  if (!browser) {
    if (callback) {
      callback(false, Json{{"message", "No browser"}});
    }
    return;
  }
  auto host = browser->GetHost();
  if (!host) {
    if (callback) {
      callback(false, Json{{"message", "No browser host"}});
    }
    return;
  }

  Attach(browser);

  int id = 0;
  {
    std::lock_guard<std::mutex> lock(g_mu);
    id = ++g_next_id;
    if (callback) {
      g_pending[id] = PendingCall{std::move(callback)};
    }
  }

  Json msg = {{"id", id}, {"method", method}};
  if (!params.is_null() && !params.empty()) {
    msg["params"] = params;
  }
  const std::string raw = msg.dump();
  if (!host->SendDevToolsMessage(raw.data(), raw.size()) && callback) {
    std::lock_guard<std::mutex> lock(g_mu);
    auto it = g_pending.find(id);
    if (it != g_pending.end()) {
      auto cb = std::move(it->second.callback);
      g_pending.erase(it);
      if (cb) {
        cb(false, Json{{"message", "SendDevToolsMessage failed"}});
      }
    }
    return;
  }

  if (!callback) {
    return;
  }
  std::thread([id]() {
    std::this_thread::sleep_for(std::chrono::seconds(8));
    DevToolsCallback cb;
    {
      std::lock_guard<std::mutex> lock(g_mu);
      auto it = g_pending.find(id);
      if (it == g_pending.end()) {
        return;
      }
      cb = std::move(it->second.callback);
      g_pending.erase(it);
    }
    if (cb) {
      CefPostTask(TID_UI, base::BindOnce(
                              [](DevToolsCallback fn) {
                                fn(false, Json{{"message", "DevTools call timed out"}});
                              },
                              std::move(cb)));
    }
  }).detach();
}

Json DevToolsClient::TakeFileChooser(int browser_id) {
  std::lock_guard<std::mutex> lock(g_mu);
  auto it = g_sessions.find(browser_id);
  if (it == g_sessions.end()) {
    return Json();
  }
  Json chooser = it->second.file_chooser;
  it->second.file_chooser = Json();
  return chooser;
}

void DevToolsClient::FailAll(const std::string& reason) {
  std::map<int, PendingCall> pending;
  {
    std::lock_guard<std::mutex> lock(g_mu);
    pending.swap(g_pending);
  }
  for (auto& entry : pending) {
    if (entry.second.callback) {
      entry.second.callback(false, Json{{"message", reason}});
    }
  }
}

void DevToolsClient::Detach(int browser_id) {
  std::lock_guard<std::mutex> lock(g_mu);
  g_sessions.erase(browser_id);
}

bool DevToolsClient::OnDevToolsMessage(CefRefPtr<CefBrowser> browser,
                                       const void* message,
                                       size_t message_size) {
  Json msg = ParseMessage(message, message_size);
  if (msg.is_discarded() || !msg.is_object()) {
    return false;
  }

  if (msg.contains("method") && msg["method"].is_string() &&
      msg["method"].get<std::string>() == "Page.fileChooserOpened") {
    if (browser) {
      std::lock_guard<std::mutex> lock(g_mu);
      g_sessions[browser->GetIdentifier()].file_chooser =
          msg.value("params", Json::object());
    }
    return true;
  }

  if (!msg.contains("id") || !msg["id"].is_number()) {
    return false;
  }
  const int id = msg["id"].get<int>();
  DevToolsCallback cb;
  {
    std::lock_guard<std::mutex> lock(g_mu);
    auto it = g_pending.find(id);
    if (it == g_pending.end()) {
      return true;
    }
    cb = std::move(it->second.callback);
    g_pending.erase(it);
  }
  if (!cb) {
    return true;
  }
  if (msg.contains("error")) {
    cb(false, msg["error"]);
  } else {
    cb(true, msg.value("result", Json::object()));
  }
  return true;
}

void DevToolsClient::OnDevToolsAgentDetached(CefRefPtr<CefBrowser> browser) {
  if (!browser) {
    return;
  }
  std::lock_guard<std::mutex> lock(g_mu);
  auto it = g_sessions.find(browser->GetIdentifier());
  if (it != g_sessions.end()) {
    it->second.intercept_on = false;
    it->second.file_chooser = Json();
  }
}

}  // namespace omni
