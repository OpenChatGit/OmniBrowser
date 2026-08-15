#pragma once

#include <cstdint>
#include <mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "omni/json.hpp"

struct OmniAdblockEngine;

namespace omni {

struct AdblockNetworkDecision {
  bool should_block = false;
  bool important = false;
  std::string redirect_name;
  std::string rewritten_url;
  std::string redirect_data_url;
};

struct AdblockCosmeticDecision {
  std::string hide_css;
  std::string injected_script;
  /** JSON string array of excepted class/id selectors for generic cosmetics. */
  std::string exceptions_json = "[]";
  bool generichide = false;
};

/** Process-wide adblock-rust engine + prefs (Brave-style, default on). */
class AdblockService {
 public:
  static AdblockService& Get();

  void Initialize();
  void Shutdown();

  bool enabled() const;
  void set_enabled(bool enabled);
  bool aggressive() const;
  void set_aggressive(bool aggressive);

  nlohmann::json PrefsJson() const;
  /** Prefs plus blockedTotal / blockedForHost (optional host filter). */
  nlohmann::json StatsJson(const std::string& host = {}) const;
  bool ApplyPrefs(const nlohmann::json& prefs);

  bool IsAllowlisted(const std::string& url) const;
  void AllowlistHost(const std::string& host, bool allow);

  int64_t blocked_total() const;
  int64_t BlockedForHost(const std::string& host) const;
  void RecordBlock(const std::string& source_url);

  AdblockNetworkDecision CheckNetwork(const std::string& url,
                                      const std::string& source_url,
                                      const std::string& request_type,
                                      const std::string& method = "GET");
  AdblockCosmeticDecision CosmeticsForUrl(const std::string& url);

  /** Generic class/id hide CSS (Brave MutationObserver path). */
  std::string HiddenClassIdCss(const std::vector<std::string>& classes,
                               const std::vector<std::string>& ids,
                               const std::string& exceptions_json);

  /** Remember a cancelled image/frame/media URL so the page can collapse it. */
  void NoteBlockedVisualUrl(const std::string& url);
  /** CSS that hides recently blocked visual resources (empty iframe/img boxes). */
  std::string VisualCollapseCss() const;

  /** Reload lists from disk (bundled + AppData). Safe to call off UI thread. */
  void ReloadEngine();

  /** Background: fetch remote lists into AppData if stale. */
  void MaybeUpdateListsAsync();

 private:
  AdblockService() = default;
  ~AdblockService();

  void LoadPrefsLocked();
  void SavePrefsLocked();
  void RecordBlockLocked(const std::string& source_url);
  void NoteBlockedVisualUrlLocked(const std::string& url);
  std::string VisualCollapseCssLocked() const;
  std::vector<std::string> CollectListPathsLocked() const;
  bool LoadListsIntoEngineLocked();
  bool IsAllowlistedLocked(const std::string& url) const;
  void ClearCosmeticCacheLocked();

  mutable std::mutex mu_;
  OmniAdblockEngine* engine_ = nullptr;
  bool initialized_ = false;
  bool enabled_ = true;
  bool aggressive_ = false;
  std::unordered_set<std::string> allowlist_;
  std::unordered_map<std::string, int64_t> blocked_by_host_;
  int64_t blocked_total_ = 0;
  int64_t blocks_since_save_ = 0;
  int64_t last_update_ts_ = 0;
  std::vector<std::string> blocked_visual_urls_;
  std::unordered_set<std::string> blocked_visual_seen_;
  mutable std::string cosmetic_cache_url_;
  mutable AdblockCosmeticDecision cosmetic_cache_;
  mutable int64_t cosmetic_cache_ts_ = 0;
};

/** Map CEF resource_type enum to adblock request_type string. */
const char* AdblockRequestTypeFromCef(int cef_resource_type);

}  // namespace omni
