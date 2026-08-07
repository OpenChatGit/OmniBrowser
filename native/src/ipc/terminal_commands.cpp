#include "omni/terminal_commands.h"

#include <cstdlib>

#include <windows.h>

#include "include/cef_parser.h"
#include "omni/omni_handler.h"
#include "omni/utf8.h"

namespace omni {
namespace {

std::string DecodeData(const Json& params) {
  if (!params.contains("data") || !params["data"].is_string()) {
    return {};
  }
  const std::string raw = params["data"].get<std::string>();
  const bool b64 = params.value("base64", false);
  if (!b64) {
    return raw;
  }
  CefRefPtr<CefBinaryValue> bin = CefBase64Decode(raw);
  if (!bin || bin->GetSize() == 0) {
    return {};
  }
  std::string out(static_cast<size_t>(bin->GetSize()), '\0');
  bin->GetData(out.data(), out.size(), 0);
  return out;
}

}  // namespace

bool HandleTerminalCommand(
    OmniHandler* owner,
    int64_t query_id,
    bool persistent,
    const std::string& method,
    const Json& params,
    CefRefPtr<CefMessageRouterBrowserSide::Callback> callback) {
  if (!owner || method.rfind("terminal.", 0) != 0) {
    return false;
  }

  auto& terminals = owner->terminals();

  if (method == "terminal.open") {
    const int cols = params.value("cols", 80);
    const int rows = params.value("rows", 24);
    std::string cwd = params.value("cwd", "");
    if (cwd.empty()) {
      // Match a normal Windows terminal: start in the user profile.
      wchar_t* profile = nullptr;
      size_t len = 0;
      if (_wdupenv_s(&profile, &len, L"USERPROFILE") == 0 && profile) {
        cwd = utf8::Narrow(profile);
        free(profile);
      }
    }

    std::string error;
    const std::string id = terminals.Open(cwd, cols, rows, &error);
    if (id.empty()) {
      callback->Failure(500, error.empty() ? "terminal.open failed" : error);
      return true;
    }

    std::string title = cwd;
    const auto slash = title.find_last_of("\\/");
    if (slash != std::string::npos && slash + 1 < title.size()) {
      title = title.substr(slash + 1);
    }
    if (title.empty()) {
      title = "Terminal";
    }

    Json out = Json::object();
    out["id"] = id;
    out["cols"] = cols;
    out["rows"] = rows;
    out["cwd"] = cwd;
    out["title"] = title;
    callback->Success(out.dump());
    return true;
  }

  if (method == "terminal.write") {
    const std::string id = params.value("id", "");
    const std::string data = DecodeData(params);
    if (id.empty() || data.empty() || !terminals.Write(id, data)) {
      callback->Failure(400, "terminal.write failed");
      return true;
    }
    callback->Success("{\"ok\":true}");
    return true;
  }

  if (method == "terminal.resize") {
    const std::string id = params.value("id", "");
    const int cols = params.value("cols", 80);
    const int rows = params.value("rows", 24);
    if (id.empty() || !terminals.Resize(id, cols, rows)) {
      callback->Failure(400, "terminal.resize failed");
      return true;
    }
    callback->Success("{\"ok\":true}");
    return true;
  }

  if (method == "terminal.close") {
    const std::string id = params.value("id", "");
    if (id.empty()) {
      callback->Failure(400, "missing id");
      return true;
    }
    terminals.Close(id);
    callback->Success("{\"ok\":true}");
    return true;
  }

  if (method == "terminal.subscribe") {
    if (!persistent) {
      callback->Failure(400, "terminal.subscribe requires persistent query");
      return true;
    }
    const std::string id = params.value("id", "");
    if (id.empty() || !terminals.Subscribe(id, query_id, callback)) {
      callback->Failure(404, "terminal session not found");
      return true;
    }
    // Keep callback alive; chunks arrive via Success() from reader thread.
    return true;
  }

  callback->Failure(404, "Unknown method: " + method);
  return true;
}

}  // namespace omni
