#pragma once

#include <string>

#include "include/cef_frame.h"

namespace omni {

/** Wikipedia / MediaWiki mutate the DOM constantly. MutationObservers and
 *  full-DOM scans in the content process OOM or CHECK-crash CEF. */
bool IsFragileDomUrl(const std::string& url);

/** Inject scrollbar CSS and a low-frequency media poll (no event flood). */
void InjectContentPageScripts(CefRefPtr<CefFrame> frame);

/** Inject cosmetic hide/:style CSS into the frame (idempotent). */
void InjectAdblockCosmeticCss(CefRefPtr<CefFrame> frame,
                              const std::string& hide_css);

/**
 * Early (OnLoadStart): hide/:style CSS + cheap YouTube player hook only.
 * Heavy observers stay off the first-paint path.
 */
void InjectAdblockCosmetics(CefRefPtr<CefFrame> frame);

/** After load: generic class/id observer + slot collapse (idle). */
void InjectAdblockObservers(CefRefPtr<CefFrame> frame);

/**
 * Brave-aligned scriptlet injection (cosmetic_filters_js_handler.cc):
 * prepends scriptletGlobals Proxy + deAmpEnabled, JSON-escapes the body,
 * injects via temporary <script> element into the page world.
 */
void InjectAdblockScriptletsBrave(CefRefPtr<CefFrame> frame,
                                  const std::string& injected_script);

/** Generic class/id MutationObserver path (skipped when generichide). */
void InjectAdblockGenericObserver(CefRefPtr<CefFrame> frame,
                                  const std::string& exceptions_json,
                                  bool generichide);

/** @deprecated Prefer InjectContentPageScripts. */
inline void InjectScrollbarStyles(CefRefPtr<CefFrame> frame) {
  InjectContentPageScripts(frame);
}

}  // namespace omni
