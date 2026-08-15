#pragma once

#include "include/cef_request_handler.h"
#include "include/cef_resource_request_handler.h"
#include "include/cef_response_filter.h"

namespace omni {

class OmniHandler;

/** Content-only network filter via adblock-rust. */
class OmniAdblockResourceHandler : public CefResourceRequestHandler {
 public:
  explicit OmniAdblockResourceHandler(OmniHandler* owner);

  CefResourceRequestHandler::ReturnValue OnBeforeResourceLoad(
      CefRefPtr<CefBrowser> browser,
      CefRefPtr<CefFrame> frame,
      CefRefPtr<CefRequest> request,
      CefRefPtr<CefCallback> callback) override;

  CefRefPtr<CefResponseFilter> GetResourceResponseFilter(
      CefRefPtr<CefBrowser> browser,
      CefRefPtr<CefFrame> frame,
      CefRefPtr<CefRequest> request,
      CefRefPtr<CefResponse> response) override;

 private:
  OmniHandler* owner_;

  IMPLEMENT_REFCOUNTING(OmniAdblockResourceHandler);
};

}  // namespace omni
