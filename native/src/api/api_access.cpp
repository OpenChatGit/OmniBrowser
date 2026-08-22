#include "omni/api/api_access.h"

#include <algorithm>
#include <cctype>
#include <filesystem>

#include "include/cef_parser.h"
#include "omni/omni_handler.h"
#include "omni/paths.h"
#include "omni/utf8.h"

namespace omni {
namespace {

std::string NormalizeFileUrlKey(const std::string& url) {
  if (url.rfind("file:", 0) != 0) {
    return {};
  }
  CefURLParts parts;
  if (!CefParseURL(url, parts)) {
    return {};
  }
  std::string path = CefString(&parts.path).ToString();
  if (path.empty()) {
    return {};
  }
  path = CefString(CefURIDecode(path, true, static_cast<cef_uri_unescape_rule_t>(
                                              UU_SPACES | UU_PATH_SEPARATORS)))
             .ToString();
  if (path.size() >= 3 && path[0] == '/' && std::isalpha(static_cast<unsigned char>(path[1])) &&
      path[2] == ':') {
    path.erase(0, 1);
  }
  std::error_code ec;
  const auto canonical = std::filesystem::weakly_canonical(
      std::filesystem::path(utf8::Widen(path)), ec);
  if (ec) {
    return {};
  }
  std::string native = utf8::Narrow(canonical.wstring());
  std::transform(native.begin(), native.end(), native.begin(),
                 [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return native;
}

bool PathUnderUiRoot(const std::string& file_url) {
  const std::string path_key = NormalizeFileUrlKey(file_url);
  if (path_key.empty()) {
    return false;
  }
  std::error_code ec;
  const auto root = std::filesystem::weakly_canonical(
      std::filesystem::path(utf8::Widen(paths::UiRootDir())), ec);
  if (ec) {
    return false;
  }
  std::string root_key = utf8::Narrow(root.wstring());
  std::transform(root_key.begin(), root_key.end(), root_key.begin(),
                 [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  if (path_key.size() < root_key.size()) {
    return false;
  }
  if (path_key.compare(0, root_key.size(), root_key) != 0) {
    return false;
  }
  if (path_key.size() == root_key.size()) {
    return true;
  }
  const char next = path_key[root_key.size()];
  return next == '\\' || next == '/';
}

bool IsWebContentAllowedMethod(std::string_view method) {
  return method == "browser.media" || method == "browser.audio" ||
         method == "browser.adblock.classId";
}

bool IsTrustedUiAllowedMethod(std::string_view method) {
  return method.rfind("history.", 0) == 0 ||
         method.rfind("bookmarks.", 0) == 0 ||
         method.rfind("downloads.", 0) == 0 ||
         method == "browser.navigate" || method == "app.info";
}

}  // namespace

bool IsTrustedUiContentBrowser(OmniHandler* owner,
                               CefRefPtr<CefBrowser> browser) {
  if (!owner || !browser || !owner->IsContentBrowser(browser)) {
    return false;
  }
  auto frame = browser->GetMainFrame();
  if (!frame) {
    return false;
  }
  return PathUnderUiRoot(frame->GetURL().ToString());
}

bool ApiAccessAllowed(const ApiContext& ctx) {
  if (!ctx.owner || !ctx.browser) {
    return false;
  }
  if (ctx.owner->IsDevToolsBrowser(ctx.browser)) {
    return false;
  }
  if (ctx.owner->IsShellBrowser(ctx.browser) ||
      ctx.owner->IsOverlayBrowser(ctx.browser)) {
    return true;
  }
  if (ctx.owner->IsContentBrowser(ctx.browser)) {
    if (IsTrustedUiContentBrowser(ctx.owner, ctx.browser) &&
        IsTrustedUiAllowedMethod(ctx.method)) {
      return true;
    }
    return IsWebContentAllowedMethod(ctx.method);
  }
  return false;
}

}  // namespace omni
