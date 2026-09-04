#pragma once

#if defined(_WIN32)
#include <windows.h>
#endif

#include <string>

namespace omni {

// "system" | "dark" | "light" from settings.json (default system).
std::string AppearancePreference();
bool ChromeShouldUseDark();
#if defined(_WIN32)
void ApplyNativeWindowAppearance(HWND hwnd);
void ApplyImmersiveMenuAppearance();
// Light/dark chrome for a native #32768 menu popup (padding + separators).
void ApplyNativeMenuAppearance(HWND hwnd);
void InstallSystemAppearanceListener(HWND hwnd);
#endif
void ApplyChromeAppearance();

}  // namespace omni
