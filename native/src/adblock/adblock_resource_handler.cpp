#include "omni/adblock_resource_handler.h"

#include <algorithm>
#include <cstring>
#include <string>

#include "include/wrapper/cef_helpers.h"
#include "omni/adblock_service.h"
#include "omni/omni_handler.h"

namespace omni {
namespace {

bool HostIsYoutube(const std::string& url) {
  const auto scheme = url.find("://");
  if (scheme == std::string::npos) {
    return false;
  }
  const size_t start = scheme + 3;
  const size_t end = url.find_first_of("/?#", start);
  const std::string host = url.substr(
      start, end == std::string::npos ? std::string::npos : end - start);
  return host.find("youtube.com") != std::string::npos ||
         host.find("youtube-nocookie.com") != std::string::npos ||
         host.find("youtubekids.com") != std::string::npos ||
         host == "youtu.be" || host == "www.youtu.be";
}

bool ShouldStripYoutubePlayerAds(const std::string& url) {
  if (!HostIsYoutube(url)) {
    return false;
  }
  // Only player payloads — never the HTML document (that delays first paint).
  return url.find("/youtubei/v1/player") != std::string::npos ||
         url.find("/youtubei/v1/get_watch") != std::string::npos ||
         url.find("/youtubei/v1/next") != std::string::npos ||
         url.find("/youtubei/v1/reel_watch") != std::string::npos;
}

void ReplaceAll(std::string* s, const char* from, const char* to) {
  size_t pos = 0;
  const size_t from_len = std::strlen(from);
  const size_t to_len = std::strlen(to);
  while ((pos = s->find(from, pos)) != std::string::npos) {
    s->replace(pos, from_len, to);
    pos += to_len;
  }
}

void StripYoutubeAdKeys(std::string* s) {
  // Same substitutions uBO/Brave apply via $replace / json-prune.
  ReplaceAll(s, "\"adPlacements\"", "\"no_ads\"");
  ReplaceAll(s, "\"adSlots\"", "\"no_ads\"");
  ReplaceAll(s, "\"playerAds\"", "\"no_ads\"");
}

// O(n) streaming replace: only the new chunk plus a 16-byte overlap is scanned.
class YoutubeAdJsonFilter : public CefResponseFilter {
 public:
  bool InitFilter() override { return true; }

  FilterStatus Filter(void* data_in,
                      size_t data_in_size,
                      size_t& data_in_read,
                      void* data_out,
                      size_t data_out_size,
                      size_t& data_out_written) override {
    data_in_read = data_in_size;
    data_out_written = 0;
    if (data_in && data_in_size > 0) {
      std::string chunk;
      chunk.reserve(tail_.size() + data_in_size);
      chunk.append(tail_);
      chunk.append(static_cast<const char*>(data_in), data_in_size);
      StripYoutubeAdKeys(&chunk);
      constexpr size_t kTail = 16;
      if (chunk.size() > kTail) {
        out_.append(chunk, 0, chunk.size() - kTail);
        tail_.assign(chunk, chunk.size() - kTail, kTail);
      } else {
        tail_.swap(chunk);
      }
    }
    if (data_in_size == 0 && !tail_.empty()) {
      out_.append(tail_);
      tail_.clear();
    }
    const size_t n = std::min(out_.size(), data_out_size);
    if (n > 0 && data_out) {
      std::memcpy(data_out, out_.data(), n);
      out_.erase(0, n);
      data_out_written = n;
    }
    if (out_.empty() && data_in_size == 0 && tail_.empty()) {
      return RESPONSE_FILTER_DONE;
    }
    return RESPONSE_FILTER_NEED_MORE_DATA;
  }

 private:
  std::string tail_;
  std::string out_;
  IMPLEMENT_REFCOUNTING(YoutubeAdJsonFilter);
};

}  // namespace

OmniAdblockResourceHandler::OmniAdblockResourceHandler(OmniHandler* owner)
    : owner_(owner) {}

CefResourceRequestHandler::ReturnValue
OmniAdblockResourceHandler::OnBeforeResourceLoad(
    CefRefPtr<CefBrowser> browser,
    CefRefPtr<CefFrame> frame,
    CefRefPtr<CefRequest> request,
    CefRefPtr<CefCallback> callback) {
  CEF_REQUIRE_IO_THREAD();
  (void)callback;
  if (!owner_ || !request) {
    return RV_CONTINUE;
  }
  if (!owner_->ShouldFilterNetwork(browser)) {
    return RV_CONTINUE;
  }

  auto& adblock = AdblockService::Get();
  if (!adblock.enabled()) {
    return RV_CONTINUE;
  }

  const std::string url = request->GetURL().ToString();

  if (url.rfind("file:", 0) == 0 || url.rfind("about:", 0) == 0 ||
      url.rfind("chrome:", 0) == 0 || url.rfind("data:", 0) == 0 ||
      url.rfind("blob:", 0) == 0) {
    return RV_CONTINUE;
  }

  std::string source;
  if (frame && frame->IsValid()) {
    source = frame->GetURL().ToString();
  }
  if (source.empty() && request) {
    source = request->GetReferrerURL().ToString();
  }

  const int cef_type = static_cast<int>(request->GetResourceType());
  if (cef_type == 0 /* RT_MAIN_FRAME */ ||
      cef_type == 19 /* RT_NAVIGATION_PRELOAD_MAIN_FRAME */) {
    return RV_CONTINUE;
  }

  const char* rtype = AdblockRequestTypeFromCef(cef_type);
  const std::string method = request->GetMethod().ToString();
  const AdblockNetworkDecision decision =
      adblock.CheckNetwork(url, source, rtype, method);

  if (!decision.rewritten_url.empty() && decision.rewritten_url != url) {
    request->SetURL(decision.rewritten_url);
  }

  if (decision.should_block) {
    return RV_CANCEL;
  }

  return RV_CONTINUE;
}

CefRefPtr<CefResponseFilter>
OmniAdblockResourceHandler::GetResourceResponseFilter(
    CefRefPtr<CefBrowser> browser,
    CefRefPtr<CefFrame> frame,
    CefRefPtr<CefRequest> request,
    CefRefPtr<CefResponse> response) {
  CEF_REQUIRE_IO_THREAD();
  (void)frame;
  (void)response;
  if (!owner_ || !request || !AdblockService::Get().enabled()) {
    return nullptr;
  }
  if (!owner_->ShouldFilterNetwork(browser)) {
    return nullptr;
  }
  const std::string url = request->GetURL().ToString();
  if (!ShouldStripYoutubePlayerAds(url)) {
    return nullptr;
  }
  return new YoutubeAdJsonFilter();
}

}  // namespace omni
