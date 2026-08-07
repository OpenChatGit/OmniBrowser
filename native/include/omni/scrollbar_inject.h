#pragma once

#include "include/cef_frame.h"

namespace omni {

/** Inject Omni content-page helpers (scrollbar CSS + media/audio probe). */
void InjectContentPageScripts(CefRefPtr<CefFrame> frame);

/** @deprecated Prefer InjectContentPageScripts. */
inline void InjectScrollbarStyles(CefRefPtr<CefFrame> frame) {
  InjectContentPageScripts(frame);
}

}  // namespace omni
