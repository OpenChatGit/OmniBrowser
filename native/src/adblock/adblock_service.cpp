#include "omni/adblock_service.h"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <thread>
#include <vector>

#include "include/base/cef_logging.h"
#include "include/cef_task.h"
#include "include/wrapper/cef_helpers.h"
#include "omni/paths.h"
#include "omni/utf8.h"
#include "omni_adblock.h"

#if defined(OS_WIN)
#include <windows.h>
#include <winhttp.h>
#pragma comment(lib, "winhttp.lib")
#endif

namespace omni {
namespace {

using Json = nlohmann::json;

std::string ReadFile(const std::filesystem::path& path) {
  std::ifstream in(path, std::ios::binary);
  if (!in) {
    return {};
  }
  return std::string((std::istreambuf_iterator<char>(in)),
                     std::istreambuf_iterator<char>());
}

bool WriteFileAtomic(const std::filesystem::path& path, const std::string& data) {
  const auto tmp_path =
      path.parent_path() / (path.filename().wstring() + L".tmp");
  {
    std::ofstream out(tmp_path, std::ios::binary | std::ios::trunc);
    if (!out) {
      return false;
    }
    out.write(data.data(), static_cast<std::streamsize>(data.size()));
  }
  std::error_code ec;
  std::filesystem::rename(tmp_path, path, ec);
  if (ec) {
    std::filesystem::remove(path, ec);
    std::filesystem::rename(tmp_path, path, ec);
  }
  return !ec;
}

std::string CssAttrEscape(const std::string& input) {
  std::string out;
  out.reserve(input.size() + 8);
  for (unsigned char c : input) {
    if (c == '\\' || c == '"' || c == '\'') {
      out.push_back('\\');
    }
    if (c >= 32) {
      out.push_back(static_cast<char>(c));
    }
  }
  return out;
}

constexpr size_t kMaxBlockedVisualUrls = 80;

std::string HostOf(const std::string& url) {
  const auto scheme = url.find("://");
  if (scheme == std::string::npos) {
    return {};
  }
  size_t start = scheme + 3;
  size_t end = url.find_first_of("/?#", start);
  if (end == std::string::npos) {
    end = url.size();
  }
  std::string host = url.substr(start, end - start);
  if (host.rfind("www.", 0) == 0) {
    host = host.substr(4);
  }
  // Strip userinfo / port
  const auto at = host.find('@');
  if (at != std::string::npos) {
    host = host.substr(at + 1);
  }
  const auto colon = host.find(':');
  if (colon != std::string::npos) {
    host = host.substr(0, colon);
  }
  return host;
}

#if defined(OS_WIN)
bool HttpGet(const std::wstring& url, std::string* out) {
  if (!out) {
    return false;
  }
  out->clear();
  URL_COMPONENTS parts{};
  parts.dwStructSize = sizeof(parts);
  wchar_t host[256] = {};
  wchar_t path[2048] = {};
  parts.lpszHostName = host;
  parts.dwHostNameLength = 256;
  parts.lpszUrlPath = path;
  parts.dwUrlPathLength = 2048;
  if (!WinHttpCrackUrl(url.c_str(), 0, 0, &parts)) {
    return false;
  }
  HINTERNET session =
      WinHttpOpen(L"OmniBrowser/1.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                  WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
  if (!session) {
    return false;
  }
  HINTERNET connect =
      WinHttpConnect(session, host, parts.nPort, 0);
  if (!connect) {
    WinHttpCloseHandle(session);
    return false;
  }
  const DWORD flags =
      (parts.nScheme == INTERNET_SCHEME_HTTPS) ? WINHTTP_FLAG_SECURE : 0;
  HINTERNET request =
      WinHttpOpenRequest(connect, L"GET", path, nullptr, WINHTTP_NO_REFERER,
                         WINHTTP_DEFAULT_ACCEPT_TYPES, flags);
  if (!request) {
    WinHttpCloseHandle(connect);
    WinHttpCloseHandle(session);
    return false;
  }
  bool ok = false;
  if (WinHttpSendRequest(request, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                         WINHTTP_NO_REQUEST_DATA, 0, 0, 0) &&
      WinHttpReceiveResponse(request, nullptr)) {
    DWORD status = 0;
    DWORD status_size = sizeof(status);
    WinHttpQueryHeaders(request,
                        WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                        WINHTTP_HEADER_NAME_BY_INDEX, &status, &status_size,
                        WINHTTP_NO_HEADER_INDEX);
    if (status >= 200 && status < 300) {
      ok = true;
      for (;;) {
        DWORD avail = 0;
        if (!WinHttpQueryDataAvailable(request, &avail) || avail == 0) {
          break;
        }
        std::string chunk(avail, '\0');
        DWORD read = 0;
        if (!WinHttpReadData(request, chunk.data(), avail, &read)) {
          ok = false;
          break;
        }
        chunk.resize(read);
        out->append(chunk);
      }
    }
  }
  WinHttpCloseHandle(request);
  WinHttpCloseHandle(connect);
  WinHttpCloseHandle(session);
  return ok;
}
#endif

}  // namespace

AdblockService& AdblockService::Get() {
  static AdblockService instance;
  return instance;
}

AdblockService::~AdblockService() {
  Shutdown();
}

void AdblockService::Initialize() {
  std::lock_guard<std::mutex> lock(mu_);
  if (initialized_) {
    return;
  }
  LoadPrefsLocked();
  engine_ = omni_adblock_create();
  if (!engine_) {
    LOG(ERROR) << "adblock-rust engine create failed";
    return;
  }
  LoadListsIntoEngineLocked();
  initialized_ = true;
}

void AdblockService::Shutdown() {
  std::lock_guard<std::mutex> lock(mu_);
  if (initialized_) {
    SavePrefsLocked();
  }
  if (engine_) {
    omni_adblock_destroy(engine_);
    engine_ = nullptr;
  }
  initialized_ = false;
}

bool AdblockService::enabled() const {
  std::lock_guard<std::mutex> lock(mu_);
  return enabled_;
}

void AdblockService::set_enabled(bool enabled) {
  std::lock_guard<std::mutex> lock(mu_);
  enabled_ = enabled;
  SavePrefsLocked();
}

bool AdblockService::aggressive() const {
  std::lock_guard<std::mutex> lock(mu_);
  return aggressive_;
}

void AdblockService::set_aggressive(bool aggressive) {
  bool need_update = false;
  {
    std::lock_guard<std::mutex> lock(mu_);
    const bool changed = aggressive_ != aggressive;
    aggressive_ = aggressive;
    SavePrefsLocked();
    if (changed && engine_) {
      LoadListsIntoEngineLocked();
    }
    // Ensure Fanboy Annoyance is on disk when enabling aggressive mode.
    if (changed && aggressive) {
      last_update_ts_ = 0;
      need_update = true;
    }
  }
  if (need_update) {
    MaybeUpdateListsAsync();
  }
}

nlohmann::json AdblockService::PrefsJson() const {
  return StatsJson();
}

nlohmann::json AdblockService::StatsJson(const std::string& host) const {
  std::lock_guard<std::mutex> lock(mu_);
  Json hosts = Json::array();
  for (const auto& h : allowlist_) {
    hosts.push_back(h);
  }
  Json out{{"enabled", enabled_},
           {"aggressive", aggressive_},
           {"allowlist", hosts},
           {"lastUpdateTs", last_update_ts_},
           {"blockedTotal", blocked_total_}};
  if (!host.empty()) {
    const std::string key = HostOf(host.find("://") != std::string::npos
                                       ? host
                                       : ("https://" + host));
    int64_t site = 0;
    if (!key.empty()) {
      const auto it = blocked_by_host_.find(key);
      if (it != blocked_by_host_.end()) {
        site = it->second;
      }
    }
    out["blockedForHost"] = site;
    out["host"] = key;
    out["siteShieldsUp"] =
        enabled_ && !key.empty() && allowlist_.count(key) == 0;
  }
  return out;
}

int64_t AdblockService::blocked_total() const {
  std::lock_guard<std::mutex> lock(mu_);
  return blocked_total_;
}

int64_t AdblockService::BlockedForHost(const std::string& host) const {
  std::lock_guard<std::mutex> lock(mu_);
  const std::string key = HostOf(host.find("://") != std::string::npos
                                     ? host
                                     : ("https://" + host));
  if (key.empty()) {
    return 0;
  }
  const auto it = blocked_by_host_.find(key);
  return it == blocked_by_host_.end() ? 0 : it->second;
}

void AdblockService::RecordBlock(const std::string& source_url) {
  std::lock_guard<std::mutex> lock(mu_);
  RecordBlockLocked(source_url);
}

void AdblockService::RecordBlockLocked(const std::string& source_url) {
  ++blocked_total_;
  ++blocks_since_save_;
  const std::string host = HostOf(source_url);
  if (!host.empty()) {
    blocked_by_host_[host] += 1;
  }
  // Persist occasionally so totals survive restarts without thrashing disk.
  if (blocks_since_save_ >= 25) {
    blocks_since_save_ = 0;
    SavePrefsLocked();
  }
}

bool AdblockService::ApplyPrefs(const nlohmann::json& prefs) {
  std::lock_guard<std::mutex> lock(mu_);
  bool reload = false;
  if (prefs.contains("enabled")) {
    enabled_ = prefs.value("enabled", true);
  }
  if (prefs.contains("aggressive")) {
    const bool next = prefs.value("aggressive", false);
    if (next != aggressive_) {
      aggressive_ = next;
      reload = true;
    }
  }
  if (prefs.contains("allowlist") && prefs["allowlist"].is_array()) {
    allowlist_.clear();
    for (const auto& h : prefs["allowlist"]) {
      if (h.is_string() && !h.get<std::string>().empty()) {
        allowlist_.insert(h.get<std::string>());
      }
    }
  }
  SavePrefsLocked();
  if (reload && engine_) {
    LoadListsIntoEngineLocked();
  }
  return true;
}

bool AdblockService::IsAllowlistedLocked(const std::string& url) const {
  const std::string host = HostOf(url);
  if (host.empty()) {
    return false;
  }
  if (allowlist_.count(host)) {
    return true;
  }
  for (const auto& entry : allowlist_) {
    if (host.size() > entry.size() &&
        host[host.size() - entry.size() - 1] == '.' &&
        host.compare(host.size() - entry.size(), entry.size(), entry) == 0) {
      return true;
    }
  }
  return false;
}

bool AdblockService::IsAllowlisted(const std::string& url) const {
  std::lock_guard<std::mutex> lock(mu_);
  return IsAllowlistedLocked(url);
}

void AdblockService::AllowlistHost(const std::string& host, bool allow) {
  std::lock_guard<std::mutex> lock(mu_);
  std::string h = host;
  if (h.rfind("www.", 0) == 0) {
    h = h.substr(4);
  }
  if (h.empty()) {
    return;
  }
  if (allow) {
    allowlist_.insert(h);
  } else {
    allowlist_.erase(h);
  }
  SavePrefsLocked();
}

AdblockNetworkDecision AdblockService::CheckNetwork(
    const std::string& url,
    const std::string& source_url,
    const std::string& request_type,
    const std::string& method) {
  AdblockNetworkDecision out;
  std::lock_guard<std::mutex> lock(mu_);
  if (!enabled_ || !engine_) {
    return out;
  }
  if (IsAllowlistedLocked(source_url.empty() ? url : source_url) ||
      IsAllowlistedLocked(url)) {
    return out;
  }
  // Skip non-http(s)
  if (url.rfind("http://", 0) != 0 && url.rfind("https://", 0) != 0) {
    return out;
  }
  const char* method_cstr =
      method.empty() ? "GET" : method.c_str();
  OmniAdblockNetworkResult r = omni_adblock_check_network(
      engine_, url.c_str(), source_url.c_str(), request_type.c_str(),
      method_cstr);
  out.should_block = r.should_block != 0;
  out.important = r.important != 0;
  if (r.redirect) {
    out.redirect_name = r.redirect;
    char* data = omni_adblock_resource_data_url(engine_, r.redirect);
    if (data) {
      out.redirect_data_url = data;
      omni_adblock_string_free(data);
    }
  }
  if (r.rewritten_url) {
    out.rewritten_url = r.rewritten_url;
  }
  omni_adblock_network_result_free(&r);
  if (out.should_block) {
    RecordBlockLocked(source_url.empty() ? url : source_url);
    if (request_type == "image" || request_type == "subdocument" ||
        request_type == "media" || request_type == "object") {
      NoteBlockedVisualUrlLocked(url);
    }
  }
  return out;
}

void AdblockService::ClearCosmeticCacheLocked() {
  cosmetic_cache_url_.clear();
  cosmetic_cache_ = {};
  cosmetic_cache_ts_ = 0;
}

AdblockCosmeticDecision AdblockService::CosmeticsForUrl(const std::string& url) {
  AdblockCosmeticDecision out;
  std::lock_guard<std::mutex> lock(mu_);
  if (!enabled_ || !engine_) {
    return out;
  }
  if (IsAllowlistedLocked(url)) {
    return out;
  }
  const int64_t now = std::chrono::duration_cast<std::chrono::seconds>(
                          std::chrono::system_clock::now().time_since_epoch())
                          .count();
  if (url == cosmetic_cache_url_ && now - cosmetic_cache_ts_ < 8) {
    return cosmetic_cache_;
  }
  OmniAdblockCosmeticResult r = omni_adblock_cosmetics(engine_, url.c_str());
  // Engine already respects $generichide when building hide_selectors.
  // hide_css also includes :style(...) layout fixes from procedural_actions.
  if (r.hide_css) {
    out.hide_css = r.hide_css;
  }
  if (r.injected_script) {
    out.injected_script = r.injected_script;
  }
  if (r.exceptions_json) {
    out.exceptions_json = r.exceptions_json;
  }
  out.generichide = r.generichide != 0;
  omni_adblock_cosmetic_result_free(&r);
  cosmetic_cache_url_ = url;
  cosmetic_cache_ = out;
  cosmetic_cache_ts_ = now;
  return out;
}

std::string AdblockService::HiddenClassIdCss(
    const std::vector<std::string>& classes,
    const std::vector<std::string>& ids,
    const std::string& exceptions_json) {
  std::lock_guard<std::mutex> lock(mu_);
  if (!enabled_ || !engine_ || (classes.empty() && ids.empty())) {
    return {};
  }
  const Json classes_j = classes;
  const Json ids_j = ids;
  const std::string classes_s = classes_j.dump();
  const std::string ids_s = ids_j.dump();
  const char* exceptions =
      exceptions_json.empty() ? "[]" : exceptions_json.c_str();
  char* css = omni_adblock_hidden_class_id_css(
      engine_, classes_s.c_str(), ids_s.c_str(), exceptions);
  if (!css) {
    return {};
  }
  std::string out = css;
  omni_adblock_string_free(css);
  return out;
}

void AdblockService::NoteBlockedVisualUrl(const std::string& url) {
  std::lock_guard<std::mutex> lock(mu_);
  NoteBlockedVisualUrlLocked(url);
}

void AdblockService::NoteBlockedVisualUrlLocked(const std::string& url) {
  if (url.rfind("http://", 0) != 0 && url.rfind("https://", 0) != 0) {
    return;
  }
  if (url.size() > 1024 || blocked_visual_seen_.count(url)) {
    return;
  }
  if (blocked_visual_urls_.size() >= kMaxBlockedVisualUrls) {
    blocked_visual_seen_.erase(blocked_visual_urls_.front());
    blocked_visual_urls_.erase(blocked_visual_urls_.begin());
  }
  blocked_visual_urls_.push_back(url);
  blocked_visual_seen_.insert(url);
}

std::string AdblockService::VisualCollapseCss() const {
  std::lock_guard<std::mutex> lock(mu_);
  return VisualCollapseCssLocked();
}

std::string AdblockService::VisualCollapseCssLocked() const {
  if (blocked_visual_urls_.empty()) {
    return {};
  }
  std::string css;
  css.reserve(blocked_visual_urls_.size() * 96);
  for (const auto& url : blocked_visual_urls_) {
    const std::string esc = CssAttrEscape(url);
    css += "iframe[src=\"";
    css += esc;
    css += "\"],img[src=\"";
    css += esc;
    css += "\"],video[src=\"";
    css += esc;
    css += "\"],embed[src=\"";
    css += esc;
    css += "\"]{display:none!important;height:0!important;min-height:0!important;";
    css += "max-height:0!important;width:0!important;min-width:0!important;";
    css += "margin:0!important;padding:0!important;overflow:hidden!important;";
    css += "border:0!important}\n";
  }
  return css;
}

void AdblockService::ReloadEngine() {
  std::lock_guard<std::mutex> lock(mu_);
  if (!engine_) {
    engine_ = omni_adblock_create();
  }
  if (engine_) {
    LoadListsIntoEngineLocked();
  }
}

void AdblockService::LoadPrefsLocked() {
  const auto path =
      std::filesystem::path(utf8::Widen(paths::AdblockPrefsPath()));
  const std::string raw = ReadFile(path);
  if (raw.empty()) {
    enabled_ = true;
    aggressive_ = false;
    blocked_total_ = 0;
    return;
  }
  Json parsed = Json::parse(raw, nullptr, false);
  if (!parsed.is_object()) {
    return;
  }
  enabled_ = parsed.value("enabled", true);
  aggressive_ = parsed.value("aggressive", false);
  last_update_ts_ = parsed.value("lastUpdateTs", static_cast<int64_t>(0));
  blocked_total_ = parsed.value("blockedTotal", static_cast<int64_t>(0));
  allowlist_.clear();
  if (parsed.contains("allowlist") && parsed["allowlist"].is_array()) {
    for (const auto& h : parsed["allowlist"]) {
      if (h.is_string()) {
        allowlist_.insert(h.get<std::string>());
      }
    }
  }
}

void AdblockService::SavePrefsLocked() {
  paths::EnsureAdblockDir();
  Json hosts = Json::array();
  for (const auto& h : allowlist_) {
    hosts.push_back(h);
  }
  const Json prefs{{"enabled", enabled_},
                   {"aggressive", aggressive_},
                   {"allowlist", hosts},
                   {"lastUpdateTs", last_update_ts_},
                   {"blockedTotal", blocked_total_}};
  WriteFileAtomic(std::filesystem::path(utf8::Widen(paths::AdblockPrefsPath())),
                  prefs.dump(2));
  blocks_since_save_ = 0;
}

std::vector<std::string> AdblockService::CollectListPathsLocked() const {
  std::vector<std::string> paths_out;
  const auto bundled =
      std::filesystem::path(utf8::Widen(paths::BundledAdblockDir()));
  const auto user =
      std::filesystem::path(utf8::Widen(paths::EnsureAdblockDir()));

  auto push_if = [&](const std::filesystem::path& p) {
    std::error_code ec;
    if (std::filesystem::exists(p, ec) && std::filesystem::is_regular_file(p, ec)) {
      paths_out.push_back(utf8::Narrow(p.wstring()));
    }
  };

  push_if(bundled / "omni-baseline.txt");
  // Brave Default Adblock + Privacy + First-Party + Cookie (desktop).
  static const char* kDefaultFiles[] = {
      "easylist.txt",
      "easyprivacy.txt",
      "ublock-filters.txt",
      "ublock-filters-2020.txt",
      "ublock-filters-2021.txt",
      "ublock-filters-2022.txt",
      "ublock-filters-2023.txt",
      "ublock-filters-2024.txt",
      "ublock-filters-2025.txt",
      "ublock-filters-2026.txt",
      "ublock-filters-general.txt",
      "ublock-badware.txt",
      "ublock-privacy.txt",
      "ublock-resource-abuse.txt",
      "ublock-quick-fixes.txt",
      "ublock-unbreak.txt",
      "ublock-link-shorteners.txt",
      "urlhaus.txt",
      "brave-unbreak.txt",
      "brave-specific.txt",
      "brave-social.txt",
      "brave-sugarcoat.txt",
      "brave-firstparty.txt",
      "brave-firstparty-regional.txt",
      "easylist-cookie.txt",
      "ublock-cookies.txt",
      "brave-cookie-specific.txt",
  };
  for (const char* name : kDefaultFiles) {
    push_if(user / name);
  }
  if (aggressive_) {
    push_if(user / "fanboy-annoyance.txt");
  }
  return paths_out;
}

bool AdblockService::LoadListsIntoEngineLocked() {
  if (!engine_) {
    return false;
  }
  const auto list_paths = CollectListPathsLocked();
  std::vector<std::string> bodies;
  bodies.reserve(list_paths.size());
  std::vector<const char*> ptrs;
  std::vector<size_t> lens;
  for (const auto& p : list_paths) {
    std::string body =
        ReadFile(std::filesystem::path(utf8::Widen(p)));
    if (body.empty()) {
      continue;
    }
    bodies.push_back(std::move(body));
  }
  if (bodies.empty()) {
    // Always have a minimal built-in list so the engine is never empty.
    bodies.push_back(
        "! Omni Browser minimal baseline\n"
        "||doubleclick.net^\n"
        "||googlesyndication.com^\n"
        "||googleadservices.com^\n"
        "||adservice.google.com^\n"
        "||pagead2.googlesyndication.com^\n"
        "||adnxs.com^\n"
        "||adsrvr.org^\n"
        "||amazon-adsystem.com^\n"
        "||scorecardresearch.com^\n"
        "||facebook.com/tr^\n");
  }
  for (const auto& b : bodies) {
    ptrs.push_back(b.data());
    lens.push_back(b.size());
  }
  const int rc =
      omni_adblock_load_lists(engine_, ptrs.data(), lens.data(), ptrs.size());
  ClearCosmeticCacheLocked();
  if (rc != 0) {
    LOG(WARNING) << "adblock load_lists failed: " << rc;
  } else {
    size_t total_bytes = 0;
    for (const auto& b : bodies) {
      total_bytes += b.size();
    }
    LOG(INFO) << "adblock loaded " << bodies.size()
              << " list(s), " << total_bytes << " bytes";
  }

  const auto res_path =
      std::filesystem::path(utf8::Widen(paths::BundledAdblockDir())) /
      "resources.json";
  const std::string resources = ReadFile(res_path);
  if (!resources.empty()) {
    omni_adblock_load_resources_json(engine_, resources.data(),
                                     resources.size());
  }
  return rc == 0;
}

namespace {

struct ListJob {
  const wchar_t* url;
  const char* filename;
};

constexpr ListJob kListJobs[] = {
    {L"https://easylist.to/easylist/easylist.txt", "easylist.txt"},
    {L"https://easylist.to/easylist/easyprivacy.txt", "easyprivacy.txt"},
    {L"https://ublockorigin.github.io/uAssets/filters/filters.txt",
     "ublock-filters.txt"},
    {L"https://ublockorigin.github.io/uAssets/filters/filters-2020.txt",
     "ublock-filters-2020.txt"},
    {L"https://ublockorigin.github.io/uAssets/filters/filters-2021.txt",
     "ublock-filters-2021.txt"},
    {L"https://ublockorigin.github.io/uAssets/filters/filters-2022.txt",
     "ublock-filters-2022.txt"},
    {L"https://ublockorigin.github.io/uAssets/filters/filters-2023.txt",
     "ublock-filters-2023.txt"},
    {L"https://ublockorigin.github.io/uAssets/filters/filters-2024.txt",
     "ublock-filters-2024.txt"},
    {L"https://ublockorigin.github.io/uAssets/filters/filters-2025.txt",
     "ublock-filters-2025.txt"},
    {L"https://ublockorigin.github.io/uAssets/filters/filters-2026.txt",
     "ublock-filters-2026.txt"},
    {L"https://ublockorigin.github.io/uAssets/filters/filters-general.txt",
     "ublock-filters-general.txt"},
    {L"https://ublockorigin.github.io/uAssets/filters/badware.txt",
     "ublock-badware.txt"},
    {L"https://ublockorigin.github.io/uAssets/filters/privacy.txt",
     "ublock-privacy.txt"},
    {L"https://ublockorigin.github.io/uAssets/filters/resource-abuse.txt",
     "ublock-resource-abuse.txt"},
    {L"https://ublockorigin.github.io/uAssets/filters/quick-fixes.txt",
     "ublock-quick-fixes.txt"},
    {L"https://ublockorigin.github.io/uAssets/filters/unbreak.txt",
     "ublock-unbreak.txt"},
    {L"https://ublockorigin.github.io/uAssets/filters/ubo-link-shorteners.txt",
     "ublock-link-shorteners.txt"},
    {L"https://malware-filter.gitlab.io/malware-filter/urlhaus-filter-agh-online.txt",
     "urlhaus.txt"},
    {L"https://raw.githubusercontent.com/brave/adblock-lists/master/brave-unbreak.txt",
     "brave-unbreak.txt"},
    {L"https://raw.githubusercontent.com/brave/adblock-lists/master/brave-lists/brave-specific.txt",
     "brave-specific.txt"},
    {L"https://raw.githubusercontent.com/brave/adblock-lists/master/brave-lists/brave-social.txt",
     "brave-social.txt"},
    {L"https://raw.githubusercontent.com/brave/adblock-lists/master/brave-lists/brave-sugarcoat.txt",
     "brave-sugarcoat.txt"},
    {L"https://raw.githubusercontent.com/brave/adblock-lists/master/brave-lists/brave-firstparty.txt",
     "brave-firstparty.txt"},
    {L"https://raw.githubusercontent.com/brave/adblock-lists/master/brave-lists/brave-firstparty-regional.txt",
     "brave-firstparty-regional.txt"},
    {L"https://secure.fanboy.co.nz/fanboy-cookiemonster_ubo.txt",
     "easylist-cookie.txt"},
    {L"https://ublockorigin.github.io/uAssets/filters/annoyances-cookies.txt",
     "ublock-cookies.txt"},
    {L"https://raw.githubusercontent.com/brave/adblock-lists/master/brave-lists/brave-cookie-specific.txt",
     "brave-cookie-specific.txt"},
    {L"https://easylist.to/easylist/fanboy-annoyance.txt",
     "fanboy-annoyance.txt"},
};

}  // namespace

void AdblockService::MaybeUpdateListsAsync() {
  int64_t last = 0;
  {
    std::lock_guard<std::mutex> lock(mu_);
    last = last_update_ts_;
  }
  const int64_t now = std::chrono::duration_cast<std::chrono::seconds>(
                          std::chrono::system_clock::now().time_since_epoch())
                          .count();
  // Any configured list missing on disk forces an update pass.
  bool missing = false;
  {
    const auto dir =
        std::filesystem::path(utf8::Widen(paths::EnsureAdblockDir()));
    std::error_code ec;
    for (const auto& job : kListJobs) {
      if (!std::filesystem::exists(dir / job.filename, ec)) {
        missing = true;
        break;
      }
    }
  }
  // Refresh at most once per 24h unless something is missing.
  if (!missing && last > 0 && (now - last) < 24 * 60 * 60) {
    return;
  }

  std::thread([this]() {
#if defined(OS_WIN)
    const auto dir =
        std::filesystem::path(utf8::Widen(paths::EnsureAdblockDir()));
    bool any = false;
    for (const auto& job : kListJobs) {
      std::string body;
      if (HttpGet(job.url, &body) && body.size() > 100) {
        if (WriteFileAtomic(dir / job.filename, body)) {
          any = true;
        }
      }
    }
    if (any) {
      {
        std::lock_guard<std::mutex> lock(mu_);
        last_update_ts_ = std::chrono::duration_cast<std::chrono::seconds>(
                              std::chrono::system_clock::now().time_since_epoch())
                              .count();
        SavePrefsLocked();
        LoadListsIntoEngineLocked();
      }
      LOG(INFO) << "adblock filter lists updated";
    }
#else
    (void)this;
#endif
  }).detach();
}

const char* AdblockRequestTypeFromCef(int cef_resource_type) {
  // Values from cef_types.h cef_resource_type_t — names match adblock-rust CPT.
  switch (cef_resource_type) {
    case 0:  // RT_MAIN_FRAME
      return "document";
    case 1:  // RT_SUB_FRAME
      return "subdocument";
    case 2:  // RT_STYLESHEET
      return "stylesheet";
    case 3:  // RT_SCRIPT
      return "script";
    case 4:  // RT_IMAGE
      return "image";
    case 5:  // RT_FONT_RESOURCE
      return "font";
    case 6:  // RT_SUB_RESOURCE
      return "other";
    case 7:  // RT_OBJECT
      return "object";
    case 8:  // RT_MEDIA
      return "media";
    case 9:  // RT_WORKER
      return "script";
    case 10:  // RT_SHARED_WORKER
      return "script";
    case 11:  // RT_PREFETCH
      return "other";
    case 12:  // RT_FAVICON
      return "image";
    case 13:  // RT_XHR
      return "xhr";
    case 14:  // RT_PING
      return "ping";
    case 15:  // RT_SERVICE_WORKER
      return "script";
    case 16:  // RT_CSP_REPORT
      return "csp_report";
    case 17:  // RT_PLUGIN_RESOURCE
      return "object";
    case 19:  // RT_NAVIGATION_PRELOAD_MAIN_FRAME
      return "document";
    case 20:  // RT_NAVIGATION_PRELOAD_SUB_FRAME
      return "subdocument";
    default:
      return "other";
  }
}

}  // namespace omni
