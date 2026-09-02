#pragma once

#if defined(_WIN32)
#include <windows.h>
#endif

#include <string>
#include <vector>

namespace omni {

// Chrome-level Comet HUD: inset blue glow around the main view plus a
// bottom-center "Agent Controlled / Take Control" pill. Lives above every
// page (Wikipedia, start, about:blank) because it is a layered HWND, not
// in-page JavaScript.
class AiHudOverlay {
 public:
  AiHudOverlay();
  ~AiHudOverlay();

  AiHudOverlay(const AiHudOverlay&) = delete;
  AiHudOverlay& operator=(const AiHudOverlay&) = delete;

#if defined(_WIN32)
  void Attach(HWND parent);
#endif
  void Detach();
  void SetActive(bool active, int agent_count);
  void Layout(int chrome_height_dip);
  // Overlay-pixel position of the visible agent cursor. click starts a
  // ripple after the move lands so the user sees the press.
  void MovePointer(float x_px, float y_px, bool click);
  bool is_active() const { return active_; }

 private:
#if defined(_WIN32)
  struct Metrics {
    int width = 0;
    int height = 0;
    RECT pill{};
    RECT button{};
  };

  static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wparam,
                                  LPARAM lparam);
  static void EnsureClass();
  int DipToPx(int dip) const;
  Metrics ComputeMetrics() const;
  bool PointInRect(POINT pt, const RECT& rc) const;
  void Paint();
  void OnClick(POINT pt);
  void OnMouseMove(POINT pt);
  void Raise();
  void ApplyVisibility();
  void SyncTimer();
  void UpdatePointer(ULONGLONG now);
  void EnsureDib(int w, int h);
  void ReleaseSurfaces();
  void RebuildGlowCache(int w, int h, int depth, float corner);
  void PaintGlow(float pulse);
  void ClearRect(const RECT& rc);
  void EnsurePillCache(const Metrics& m);
  void EnsureCursorCache();
  void BlitSprite(int dx, int dy, HBITMAP src, int sw, int sh);
  RECT PointerDirtyRect(float x, float y) const;
  void Present(const RECT* dirty);

  struct GlowSample {
    UINT32 offset = 0;
    BYTE falloff = 0;
  };

  HWND parent_ = nullptr;
  HWND hwnd_ = nullptr;
  bool hover_button_ = false;
  UINT_PTR timer_ = 0;
  UINT timer_ms_ = 0;
  ULONGLONG start_tick_ = 0;
  bool cursor_shown_ = false;
  float cursor_x_ = -1.0f;
  float cursor_y_ = -1.0f;
  float cursor_from_x_ = 0.0f;
  float cursor_from_y_ = 0.0f;
  float cursor_to_x_ = 0.0f;
  float cursor_to_y_ = 0.0f;
  ULONGLONG cursor_anim_start_ = 0;
  int cursor_anim_ms_ = 0;
  ULONGLONG click_pulse_start_ = 0;

  HDC mem_dc_ = nullptr;
  HBITMAP dib_ = nullptr;
  HGDIOBJ mem_old_ = nullptr;
  void* bits_ = nullptr;
  int dib_w_ = 0;
  int dib_h_ = 0;
  int dib_stride_ = 0;
  std::vector<GlowSample> glow_samples_;
  int glow_w_ = 0;
  int glow_h_ = 0;
  int glow_depth_ = 0;
  float glow_corner_ = -1.0f;
  RECT last_cursor_rc_{};
  HBITMAP pill_dib_ = nullptr;
  void* pill_bits_ = nullptr;
  int pill_w_ = 0;
  int pill_h_ = 0;
  int pill_cache_count_ = -1;
  bool pill_cache_hover_ = false;
  HBITMAP cursor_dib_ = nullptr;
  void* cursor_bits_ = nullptr;
  int cursor_dib_w_ = 0;
  int cursor_dib_h_ = 0;
  int cursor_hot_x_ = 0;
  int cursor_hot_y_ = 0;
#endif
  bool active_ = false;
  int agent_count_ = 0;
  int chrome_height_dip_ = 80;
};

}  // namespace omni
