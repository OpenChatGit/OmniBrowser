#include "omni/api/api_dispatcher.h"

#include <algorithm>

namespace omni {

ApiDispatcher& ApiDispatcher::Get() {
  static ApiDispatcher instance;
  return instance;
}

void ApiDispatcher::Register(std::string_view method,
                             ApiHandler handler,
                             ApiExposure exposure) {
  exact_[std::string(method)] = Entry{std::move(handler), exposure};
}

void ApiDispatcher::RegisterPrefix(std::string_view prefix,
                                   ApiHandler handler,
                                   ApiExposure exposure) {
  prefixes_.emplace_back(std::string(prefix),
                         Entry{std::move(handler), exposure});
  std::sort(prefixes_.begin(), prefixes_.end(),
            [](const auto& a, const auto& b) {
              return a.first.size() > b.first.size();
            });
}

bool ApiDispatcher::Dispatch(std::string_view method,
                             const ApiContext& ctx,
                             const Json& params,
                             ApiResponder& responder) const {
  const auto exact = exact_.find(std::string(method));
  if (exact != exact_.end()) {
    exact->second.handler(ctx, params, responder);
    return true;
  }
  for (const auto& entry : prefixes_) {
    if (method.rfind(entry.first, 0) == 0) {
      entry.second.handler(ctx, params, responder);
      return true;
    }
  }
  return false;
}

bool ApiDispatcher::IsRemoteSafe(std::string_view method) const {
  const auto exact = exact_.find(std::string(method));
  if (exact != exact_.end()) {
    return exact->second.exposure == ApiExposure::RemoteSafe;
  }
  for (const auto& entry : prefixes_) {
    if (method.rfind(entry.first, 0) == 0) {
      return entry.second.exposure == ApiExposure::RemoteSafe;
    }
  }
  return false;
}

}  // namespace omni
