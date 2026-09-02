#include "omni/ai_hud.h"

#include "omni/log.h"
#include "omni/mcp/mcp_server.h"

#if defined(_WIN32)

#include <windowsx.h>
#include <objidl.h>
#include <gdiplus.h>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <string>
#include <vector>
#include <wchar.h>

#pragma comment(lib, "gdiplus.lib")
#pragma comment(lib, "msimg32.lib")

namespace omni {
namespace {

constexpr wchar_t kClassName[] = L"OmniAiHud";
constexpr int kTimerId = 1;

ULONG_PTR g_gdiplus_token = 0;
int g_gdiplus_refs = 0;

void GdiplusAddRef() {
  if (g_gdiplus_refs++ == 0) {
    Gdiplus::GdiplusStartupInput input;
    Gdiplus::GdiplusStartup(&g_gdiplus_token, &input, nullptr);
  }
}

void GdiplusRelease() {
  if (g_gdiplus_refs > 0 && --g_gdiplus_refs == 0 && g_gdiplus_token) {
    Gdiplus::GdiplusShutdown(g_gdiplus_token);
    g_gdiplus_token = 0;
  }
}

int ClampInt(int v, int lo, int hi) {
  return std::max(lo, std::min(v, hi));
}

// Distance from (x,y) to the inside of a rect whose bottom corners are
// rounded. Negative = outside the quarter-circles (the square overflow).
float EdgeDistance(float x, float y, float w, float h, float radius) {
  if (radius > 0.5f) {
    if (x < radius && y > h - radius) {
      const float dx = radius - x;
      const float dy = y - (h - radius);
      return radius - std::sqrt(dx * dx + dy * dy);
    }
    if (x > w - radius && y > h - radius) {
      const float dx = x - (w - radius);
      const float dy = y - (h - radius);
      return radius - std::sqrt(dx * dx + dy * dy);
    }
  }
  return (std::min)((std::min)(x, w - x), (std::min)(y, h - y));
}

HBITMAP CreatePargbDib(int w, int h, void** bits) {
  BITMAPINFO bmi = {};
  bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
  bmi.bmiHeader.biWidth = w;
  bmi.bmiHeader.biHeight = -h;
  bmi.bmiHeader.biPlanes = 1;
  bmi.bmiHeader.biBitCount = 32;
  bmi.bmiHeader.biCompression = BI_RGB;
  HDC screen = GetDC(nullptr);
  HBITMAP dib =
      CreateDIBSection(screen, &bmi, DIB_RGB_COLORS, bits, nullptr, 0);
  ReleaseDC(nullptr, screen);
  if (dib && bits && *bits) {
    std::memset(*bits, 0, static_cast<size_t>(w) * h * 4);
  }
  return dib;
}

void BlitPremul(BYTE* dst,
                int dst_w,
                int dst_h,
                int dst_stride,
                int dx,
                int dy,
                const BYTE* src,
                int src_w,
                int src_h,
                int src_stride) {
  if (!dst || !src) {
    return;
  }
  for (int sy = 0; sy < src_h; ++sy) {
    const int y = dy + sy;
    if (y < 0 || y >= dst_h) {
      continue;
    }
    const BYTE* srow = src + static_cast<size_t>(sy) * src_stride;
    BYTE* drow = dst + static_cast<size_t>(y) * dst_stride;
    for (int sx = 0; sx < src_w; ++sx) {
      const int x = dx + sx;
      if (x < 0 || x >= dst_w) {
        continue;
      }
      const BYTE* s = srow + sx * 4;
      if (s[3] == 0) {
        continue;
      }
      BYTE* d = drow + x * 4;
      if (s[3] == 255 || d[3] == 0) {
        d[0] = s[0];
        d[1] = s[1];
        d[2] = s[2];
        d[3] = s[3];
        continue;
      }
      const int inv = 255 - s[3];
      d[0] = static_cast<BYTE>(s[0] + (d[0] * inv) / 255);
      d[1] = static_cast<BYTE>(s[1] + (d[1] * inv) / 255);
      d[2] = static_cast<BYTE>(s[2] + (d[2] * inv) / 255);
      d[3] = static_cast<BYTE>(s[3] + (d[3] * inv) / 255);
    }
  }
}

void DrawPulseRing(BYTE* bits,
                   int w,
                   int h,
                   int stride,
                   float cx,
                   float cy,
                   float t,
                   float dip) {
  if (!bits || t <= 0.0f || t >= 1.0f) {
    return;
  }
  const float r = (10.0f + t * 26.0f) * dip;
  const int alpha = ClampInt(static_cast<int>((1.0f - t) * 160.0f), 0, 255);
  const int x0 = ClampInt(static_cast<int>(cx - r - 2), 0, w);
  const int y0 = ClampInt(static_cast<int>(cy - r - 2), 0, h);
  const int x1 = ClampInt(static_cast<int>(cx + r + 3), 0, w);
  const int y1 = ClampInt(static_cast<int>(cy + r + 3), 0, h);
  const float inner = r - 1.4f * dip;
  const float outer = r + 1.4f * dip;
  for (int y = y0; y < y1; ++y) {
    BYTE* row = bits + static_cast<size_t>(y) * stride;
    for (int x = x0; x < x1; ++x) {
      const float dx = (x + 0.5f) - cx;
      const float dy = (y + 0.5f) - cy;
      const float dist = std::sqrt(dx * dx + dy * dy);
      if (dist < inner || dist > outer) {
        continue;
      }
      float cov = 1.0f;
      if (dist < inner + 1.0f) {
        cov = dist - inner;
      } else if (dist > outer - 1.0f) {
        cov = outer - dist;
      }
      const int a = ClampInt(static_cast<int>(alpha * cov), 0, 255);
      if (a <= 0) {
        continue;
      }
      BYTE* p = row + x * 4;
      p[0] = static_cast<BYTE>((244 * a) / 255);
      p[1] = static_cast<BYTE>((192 * a) / 255);
      p[2] = static_cast<BYTE>((102 * a) / 255);
      p[3] = static_cast<BYTE>(a);
    }
  }
}

void FillAlignedText(Gdiplus::Graphics* g,
                     const wchar_t* text,
                     const Gdiplus::FontFamily& family,
                     INT style,
                     Gdiplus::REAL em_size,
                     const Gdiplus::RectF& box,
                     const Gdiplus::Brush& brush,
                     Gdiplus::StringAlignment align) {
  Gdiplus::GraphicsPath glyphs;
  Gdiplus::StringFormat fmt(Gdiplus::StringFormat::GenericTypographic());
  fmt.SetAlignment(align);
  fmt.SetLineAlignment(Gdiplus::StringAlignmentCenter);
  fmt.SetFormatFlags(Gdiplus::StringFormatFlagsNoWrap |
                     Gdiplus::StringFormatFlagsNoClip);
  glyphs.AddString(text, -1, &family, style, em_size, box, &fmt);
  g->FillPath(&brush, &glyphs);
}

float SdRoundBox(float px, float py, float bw, float bh, float r) {
  const float half_w = bw * 0.5f;
  const float half_h = bh * 0.5f;
  const float rr = (std::min)(r, (std::min)(half_w, half_h));
  const float dx = std::fabs(px - half_w) - (half_w - rr);
  const float dy = std::fabs(py - half_h) - (half_h - rr);
  const float ox = (std::max)(dx, 0.0f);
  const float oy = (std::max)(dy, 0.0f);
  return (std::min)((std::max)(dx, dy), 0.0f) +
         std::sqrt(ox * ox + oy * oy) - rr;
}

float EdgeCoverage(float sd) {
  return (std::max)(0.0f, (std::min)(1.0f, 0.5f - sd));
}

void PremulOver(BYTE* d, int sr, int sg, int sb, int sa) {
  if (sa <= 0) {
    return;
  }
  if (sa >= 255 || d[3] == 0) {
    d[0] = static_cast<BYTE>(sb);
    d[1] = static_cast<BYTE>(sg);
    d[2] = static_cast<BYTE>(sr);
    d[3] = static_cast<BYTE>(sa);
    return;
  }
  const int inv = 255 - sa;
  d[0] = static_cast<BYTE>(sb + (d[0] * inv) / 255);
  d[1] = static_cast<BYTE>(sg + (d[1] * inv) / 255);
  d[2] = static_cast<BYTE>(sr + (d[2] * inv) / 255);
  d[3] = static_cast<BYTE>(sa + (d[3] * inv) / 255);
}

// Solid rounded-rect into a PARGB DIB. Edge pixels use the fill color ×
// coverage so layered-window AA stays dark — never a white GDI+ fringe.
void BlendRoundRect(BYTE* bits,
                    int w,
                    int h,
                    int stride,
                    float x,
                    float y,
                    float bw,
                    float bh,
                    float radius,
                    BYTE cr,
                    BYTE cg,
                    BYTE cb,
                    BYTE ca) {
  if (!bits || bw < 1.0f || bh < 1.0f || ca == 0) {
    return;
  }
  const int x0 = ClampInt(static_cast<int>(std::floor(x - 1.0f)), 0, w);
  const int y0 = ClampInt(static_cast<int>(std::floor(y - 1.0f)), 0, h);
  const int x1 = ClampInt(static_cast<int>(std::ceil(x + bw + 1.0f)), 0, w);
  const int y1 = ClampInt(static_cast<int>(std::ceil(y + bh + 1.0f)), 0, h);
  for (int py = y0; py < y1; ++py) {
    BYTE* row = bits + static_cast<size_t>(py) * stride;
    for (int px = x0; px < x1; ++px) {
      const float sd =
          SdRoundBox((px + 0.5f) - x, (py + 0.5f) - y, bw, bh, radius);
      const float cov = EdgeCoverage(sd);
      if (cov <= 0.001f) {
        continue;
      }
      const int sa = ClampInt(static_cast<int>(ca * cov + 0.5f), 0, 255);
      PremulOver(row + px * 4, (cr * sa) / 255, (cg * sa) / 255,
                 (cb * sa) / 255, sa);
    }
  }
}

void BlendRoundRectRing(BYTE* bits,
                        int w,
                        int h,
                        int stride,
                        float x,
                        float y,
                        float bw,
                        float bh,
                        float radius,
                        float thickness,
                        BYTE cr,
                        BYTE cg,
                        BYTE cb,
                        BYTE ca) {
  if (!bits || thickness <= 0.0f || ca == 0) {
    return;
  }
  const int x0 = ClampInt(static_cast<int>(std::floor(x - 1.0f)), 0, w);
  const int y0 = ClampInt(static_cast<int>(std::floor(y - 1.0f)), 0, h);
  const int x1 = ClampInt(static_cast<int>(std::ceil(x + bw + 1.0f)), 0, w);
  const int y1 = ClampInt(static_cast<int>(std::ceil(y + bh + 1.0f)), 0, h);
  for (int py = y0; py < y1; ++py) {
    BYTE* row = bits + static_cast<size_t>(py) * stride;
    for (int px = x0; px < x1; ++px) {
      const float sd =
          SdRoundBox((px + 0.5f) - x, (py + 0.5f) - y, bw, bh, radius);
      const float cov =
          EdgeCoverage(sd) - EdgeCoverage(sd + thickness);
      if (cov <= 0.001f) {
        continue;
      }
      const int sa = ClampInt(static_cast<int>(ca * cov + 0.5f), 0, 255);
      PremulOver(row + px * 4, (cr * sa) / 255, (cg * sa) / 255,
                 (cb * sa) / 255, sa);
    }
  }
}

void PremultiplyArgb(void* bits, int w, int h, int stride) {
  auto* rows = static_cast<BYTE*>(bits);
  for (int y = 0; y < h; ++y) {
    BYTE* row = rows + static_cast<size_t>(y) * stride;
    for (int x = 0; x < w; ++x) {
      BYTE* p = row + x * 4;
      const unsigned a = p[3];
      if (a == 0) {
        p[0] = p[1] = p[2] = 0;
        continue;
      }
      if (a == 255) {
        continue;
      }
      p[0] = static_cast<BYTE>((p[0] * a) / 255);
      p[1] = static_cast<BYTE>((p[1] * a) / 255);
      p[2] = static_cast<BYTE>((p[2] * a) / 255);
    }
  }
}

// Windows Aero pointer already on the machine (Win7–11). Standard
// size — not the extra-large scheme file.
HCURSOR LoadSystemArrowCursor(int size, bool* owned) {
  *owned = false;
  wchar_t windir[MAX_PATH] = {};
  const UINT n = GetWindowsDirectoryW(windir, MAX_PATH);
  if (n > 0 && n < MAX_PATH) {
    static const wchar_t* kFiles[] = {
        L"\\Cursors\\aero_arrow.cur",
        L"\\Cursors\\aero_arrow_l.cur",
        L"\\Cursors\\aero_arrow_xl.cur",
    };
    wchar_t path[MAX_PATH];
    for (const wchar_t* rel : kFiles) {
      if (wcslen(windir) + wcslen(rel) >= MAX_PATH) {
        continue;
      }
      wcscpy_s(path, windir);
      wcscat_s(path, rel);
      HCURSOR cursor = reinterpret_cast<HCURSOR>(LoadImageW(
          nullptr, path, IMAGE_CURSOR, size, size, LR_LOADFROMFILE));
      if (cursor) {
        *owned = true;
        return cursor;
      }
    }
  }
  return LoadCursorW(nullptr, IDC_ARROW);
}

int SnapCursorSize(int dip_px) {
  if (dip_px >= 40) {
    return 48;
  }
  return 32;
}

}  // namespace

AiHudOverlay::AiHudOverlay() = default;

AiHudOverlay::~AiHudOverlay() {
  Detach();
}

void AiHudOverlay::EnsureClass() {
  static bool registered = false;
  if (registered) {
    return;
  }
  WNDCLASSEXW wc = {};
  wc.cbSize = sizeof(wc);
  wc.lpfnWndProc = AiHudOverlay::WndProc;
  wc.hInstance = GetModuleHandleW(nullptr);
  wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
  wc.lpszClassName = kClassName;
  RegisterClassExW(&wc);
  registered = true;
}

int AiHudOverlay::DipToPx(int dip) const {
  HWND src = hwnd_ ? hwnd_ : parent_;
  UINT dpi = src ? GetDpiForWindow(src) : 96;
  if (dpi == 0) {
    dpi = 96;
  }
  return MulDiv(dip, static_cast<int>(dpi), 96);
}

void AiHudOverlay::Attach(HWND parent) {
  if (!parent) {
    return;
  }
  if (hwnd_ && parent_ == parent) {
    return;
  }
  Detach();
  GdiplusAddRef();
  EnsureClass();
  parent_ = parent;
  // Owned popup, not a CEF child. Alloy content HWNDs always stack above
  // layered WS_CHILD windows, so the HUD would be invisible on Wikipedia.
  hwnd_ = CreateWindowExW(
      WS_EX_LAYERED | WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW,
      kClassName, L"", WS_POPUP, 0, 0, 1, 1, parent, nullptr,
      GetModuleHandleW(nullptr), this);
  if (!hwnd_) {
    omni::Log("AiHud: CreateWindowEx failed err=" +
              std::to_string(static_cast<unsigned>(GetLastError())));
    GdiplusRelease();
    parent_ = nullptr;
    return;
  }
  SetWindowLongPtrW(hwnd_, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(this));
  start_tick_ = GetTickCount64();
  omni::Log("AiHud: attached popup overlay");
}

void AiHudOverlay::Detach() {
  HWND hwnd = hwnd_;
  hwnd_ = nullptr;
  parent_ = nullptr;
  hover_button_ = false;
  if (!hwnd) {
    return;
  }
  if (timer_) {
    KillTimer(hwnd, timer_);
    timer_ = 0;
  }
  ReleaseSurfaces();
  DestroyWindow(hwnd);
  GdiplusRelease();
}

void AiHudOverlay::SetActive(bool active, int agent_count) {
  active_ = active;
  agent_count_ = active ? std::max(1, agent_count) : 0;
  if (!active_) {
    cursor_shown_ = false;
    cursor_x_ = -1.0f;
    cursor_y_ = -1.0f;
    cursor_anim_ms_ = 0;
    click_pulse_start_ = 0;
  } else {
    cursor_shown_ = true;
  }
  if (hwnd_) {
    SyncTimer();
  } else if (active_) {
    start_tick_ = GetTickCount64();
  }
  if (active_) {
    omni::Log("AiHud: active agents=" + std::to_string(agent_count_));
  }
  ApplyVisibility();
}

void AiHudOverlay::Layout(int chrome_height_dip) {
  chrome_height_dip_ = std::max(0, chrome_height_dip);
  if (!parent_ || !hwnd_) {
    return;
  }
  RECT rc = {};
  GetClientRect(parent_, &rc);
  const int client_w = static_cast<int>(rc.right);
  const int client_h = static_cast<int>(rc.bottom);
  const int chrome_px = DipToPx(chrome_height_dip_);
  const int y_off = ClampInt(chrome_px, 0, std::max(0, client_h - 8));
  const int w = std::max(0, client_w);
  const int h = std::max(0, client_h - y_off);
  if (w < 8 || h < 8) {
    omni::Log("AiHud: layout skipped size=" + std::to_string(w) + "x" +
              std::to_string(h));
    return;
  }
  POINT origin = {0, y_off};
  ClientToScreen(parent_, &origin);
  SetWindowPos(hwnd_, HWND_TOP, origin.x, origin.y, w, h,
               SWP_NOACTIVATE);
  ApplyVisibility();
}

void AiHudOverlay::Raise() {
  if (hwnd_ && active_) {
    SetWindowPos(hwnd_, HWND_TOP, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_SHOWWINDOW);
  }
}

void AiHudOverlay::ApplyVisibility() {
  if (!hwnd_) {
    return;
  }
  ShowWindow(hwnd_, active_ ? SW_SHOWNOACTIVATE : SW_HIDE);
  if (active_) {
    Raise();
    Paint();
  }
}

void AiHudOverlay::SyncTimer() {
  if (!hwnd_) {
    return;
  }
  if (!active_) {
    if (timer_) {
      KillTimer(hwnd_, timer_);
      timer_ = 0;
    }
    timer_ms_ = 0;
    return;
  }
  if (!start_tick_) {
    start_tick_ = GetTickCount64();
  }
  const UINT ms = 16;
  if (timer_ && timer_ms_ == ms) {
    return;
  }
  if (timer_) {
    KillTimer(hwnd_, timer_);
  }
  timer_ = SetTimer(hwnd_, kTimerId, ms, nullptr);
  timer_ms_ = ms;
}

void AiHudOverlay::MovePointer(float x_px, float y_px, bool click) {
  if (!active_) {
    return;
  }
  cursor_shown_ = true;
  if (cursor_x_ < 0.0f) {
    cursor_x_ = x_px;
    cursor_y_ = y_px;
  }
  cursor_from_x_ = cursor_x_;
  cursor_from_y_ = cursor_y_;
  cursor_to_x_ = x_px;
  cursor_to_y_ = y_px;
  cursor_anim_start_ = GetTickCount64();
  cursor_anim_ms_ = 260;
  click_pulse_start_ = click ? cursor_anim_start_ + cursor_anim_ms_ : 0;
  SyncTimer();
  Paint();
}

void AiHudOverlay::UpdatePointer(ULONGLONG now) {
  if (!cursor_shown_) {
    return;
  }
  if (cursor_anim_ms_ > 0) {
    float u = static_cast<float>(now - cursor_anim_start_) /
              static_cast<float>(cursor_anim_ms_);
    if (u >= 1.0f) {
      u = 1.0f;
      cursor_anim_ms_ = 0;
    }
    const float e = 1.0f - (1.0f - u) * (1.0f - u) * (1.0f - u);
    cursor_x_ = cursor_from_x_ + (cursor_to_x_ - cursor_from_x_) * e;
    cursor_y_ = cursor_from_y_ + (cursor_to_y_ - cursor_from_y_) * e;
  }
}

AiHudOverlay::Metrics AiHudOverlay::ComputeMetrics() const {
  Metrics m;
  if (!hwnd_) {
    return m;
  }
  RECT rc = {};
  GetClientRect(hwnd_, &rc);
  m.width = rc.right;
  m.height = rc.bottom;
  const int pill_h = DipToPx(42);
  const int pill_w = DipToPx(268);
  const int btn_w = DipToPx(108);
  const int btn_h = DipToPx(28);
  const int margin = DipToPx(24);
  m.pill.left = (m.width - pill_w) / 2;
  m.pill.right = m.pill.left + pill_w;
  m.pill.top = m.height - margin - pill_h;
  m.pill.bottom = m.pill.top + pill_h;
  m.button.right = m.pill.right - DipToPx(7);
  m.button.left = m.button.right - btn_w;
  m.button.top = m.pill.top + (pill_h - btn_h) / 2;
  m.button.bottom = m.button.top + btn_h;
  return m;
}

bool AiHudOverlay::PointInRect(POINT pt, const RECT& rc) const {
  return PtInRect(&rc, pt) != 0;
}

void AiHudOverlay::ReleaseSurfaces() {
  if (mem_dc_) {
    if (mem_old_) {
      SelectObject(mem_dc_, mem_old_);
    }
    DeleteDC(mem_dc_);
  }
  mem_dc_ = nullptr;
  mem_old_ = nullptr;
  if (dib_) {
    DeleteObject(dib_);
  }
  dib_ = nullptr;
  bits_ = nullptr;
  dib_w_ = 0;
  dib_h_ = 0;
  dib_stride_ = 0;
  glow_samples_.clear();
  glow_w_ = 0;
  glow_h_ = 0;
  glow_depth_ = 0;
  glow_corner_ = -1.0f;
  if (pill_dib_) {
    DeleteObject(pill_dib_);
  }
  pill_dib_ = nullptr;
  pill_bits_ = nullptr;
  pill_w_ = 0;
  pill_h_ = 0;
  pill_cache_count_ = -1;
  if (cursor_dib_) {
    DeleteObject(cursor_dib_);
  }
  cursor_dib_ = nullptr;
  cursor_bits_ = nullptr;
  cursor_dib_w_ = 0;
  cursor_dib_h_ = 0;
  last_cursor_rc_ = {};
}

void AiHudOverlay::EnsureDib(int w, int h) {
  if (dib_ && dib_w_ == w && dib_h_ == h && bits_ && mem_dc_) {
    return;
  }
  if (mem_dc_) {
    if (mem_old_) {
      SelectObject(mem_dc_, mem_old_);
    }
    DeleteDC(mem_dc_);
    mem_dc_ = nullptr;
    mem_old_ = nullptr;
  }
  if (dib_) {
    DeleteObject(dib_);
  }
  bits_ = nullptr;
  dib_ = CreatePargbDib(w, h, &bits_);
  dib_w_ = w;
  dib_h_ = h;
  dib_stride_ = w * 4;
  if (!dib_ || !bits_) {
    dib_ = nullptr;
    bits_ = nullptr;
    return;
  }
  mem_dc_ = CreateCompatibleDC(nullptr);
  mem_old_ = SelectObject(mem_dc_, dib_);
  glow_w_ = 0;
}

void AiHudOverlay::RebuildGlowCache(int w, int h, int depth, float corner) {
  if (glow_w_ == w && glow_h_ == h && glow_depth_ == depth &&
      glow_corner_ == corner) {
    return;
  }
  glow_samples_.clear();
  glow_w_ = w;
  glow_h_ = h;
  glow_depth_ = depth;
  glow_corner_ = corner;
  if (w < 1 || h < 1 || depth < 1 || !bits_) {
    return;
  }
  glow_samples_.reserve(static_cast<size_t>((w + h) * 2 * (depth + 4)));
  const float wf = static_cast<float>(w);
  const float hf = static_cast<float>(h);
  const float df = static_cast<float>(depth);
  const int y_in0 = depth + 2;
  const int y_in1 = h - depth - 2;
  const int x_in0 = depth + 2;
  const int x_in1 = w - depth - 2;
  for (int py = 0; py < h; ++py) {
    const bool y_edge = py < y_in0 || py >= y_in1;
    for (int px = 0; px < w; ++px) {
      if (!y_edge && px >= x_in0 && px < x_in1) {
        continue;
      }
      const float d = EdgeDistance(px + 0.5f, py + 0.5f, wf, hf, corner);
      if (d < -1.0f || d > df) {
        continue;
      }
      float t = 1.0f - d / df;
      if (d < 0.0f) {
        t *= (1.0f + d);
      }
      if (t <= 0.0f) {
        continue;
      }
      GlowSample s;
      s.offset = static_cast<UINT32>(py * dib_stride_ + px * 4);
      s.falloff = static_cast<BYTE>(ClampInt(static_cast<int>(t * 255.0f), 1, 255));
      glow_samples_.push_back(s);
    }
  }
  std::memset(bits_, 0, static_cast<size_t>(dib_stride_) * h);
}

void AiHudOverlay::PaintGlow(float pulse) {
  if (!bits_) {
    return;
  }
  const int pulse_q = ClampInt(static_cast<int>(pulse * 256.0f), 0, 256);
  for (const GlowSample& s : glow_samples_) {
    const int t = s.falloff;
    const int alpha =
        (t * t * pulse_q * 110) / (255 * 255 * 256);
    BYTE* p = static_cast<BYTE*>(bits_) + s.offset;
    if (alpha <= 0) {
      p[0] = 0;
      p[1] = 0;
      p[2] = 0;
      p[3] = 0;
      continue;
    }
    p[0] = static_cast<BYTE>((244 * alpha) / 255);
    p[1] = static_cast<BYTE>((192 * alpha) / 255);
    p[2] = static_cast<BYTE>((102 * alpha) / 255);
    p[3] = static_cast<BYTE>(alpha);
  }
}

void AiHudOverlay::ClearRect(const RECT& rc) {
  if (!bits_ || rc.right <= rc.left || rc.bottom <= rc.top) {
    return;
  }
  const int x0 = ClampInt(rc.left, 0, dib_w_);
  const int y0 = ClampInt(rc.top, 0, dib_h_);
  const int x1 = ClampInt(rc.right, 0, dib_w_);
  const int y1 = ClampInt(rc.bottom, 0, dib_h_);
  const int bytes = (x1 - x0) * 4;
  if (bytes <= 0) {
    return;
  }
  auto* base = static_cast<BYTE*>(bits_);
  for (int y = y0; y < y1; ++y) {
    std::memset(base + static_cast<size_t>(y) * dib_stride_ + x0 * 4, 0, bytes);
  }
}

void AiHudOverlay::EnsurePillCache(const Metrics& m) {
  const int w = m.pill.right - m.pill.left;
  const int h = m.pill.bottom - m.pill.top;
  if (w < 8 || h < 8) {
    return;
  }
  if (pill_dib_ && pill_w_ == w && pill_h_ == h &&
      pill_cache_count_ == agent_count_ && pill_cache_hover_ == hover_button_) {
    return;
  }
  if (pill_dib_) {
    DeleteObject(pill_dib_);
    pill_dib_ = nullptr;
    pill_bits_ = nullptr;
  }
  pill_dib_ = CreatePargbDib(w, h, &pill_bits_);
  pill_w_ = w;
  pill_h_ = h;
  pill_cache_count_ = agent_count_;
  pill_cache_hover_ = hover_button_;
  if (!pill_dib_ || !pill_bits_) {
    return;
  }

  auto* bits = static_cast<BYTE*>(pill_bits_);
  const int stride = w * 4;
  // Keep 1px of empty coverage around the stadium so AA is not clipped
  // (clipped AA + a white GDI+ stroke was the jagged halo).
  const float pad = 1.0f;
  const float bx = pad;
  const float by = pad;
  const float bw = static_cast<float>(w) - pad * 2.0f;
  const float bh = static_cast<float>(h) - pad * 2.0f;
  const float radius = bh * 0.5f;
  BlendRoundRect(bits, w, h, stride, bx, by, bw, bh, radius, 26, 25, 26, 255);
  BlendRoundRectRing(bits, w, h, stride, bx + 0.75f, by + 0.75f, bw - 1.5f,
                     bh - 1.5f, (std::max)(1.0f, radius - 0.75f), 1.0f, 255,
                     255, 255, 36);
  const float btn_x =
      static_cast<float>(m.button.left - m.pill.left);
  const float btn_y = static_cast<float>(m.button.top - m.pill.top);
  const float btn_w = static_cast<float>(m.button.right - m.button.left);
  const float btn_h = static_cast<float>(m.button.bottom - m.button.top);
  const float btn_r = btn_h * 0.5f;
  if (hover_button_) {
    BlendRoundRect(bits, w, h, stride, btn_x, btn_y, btn_w, btn_h, btn_r, 217,
                   75, 75, 56);
    BlendRoundRectRing(bits, w, h, stride, btn_x + 0.6f, btn_y + 0.6f,
                       btn_w - 1.2f, btn_h - 1.2f,
                       (std::max)(1.0f, btn_r - 0.6f), 1.0f, 217, 75, 75, 128);
  } else {
    BlendRoundRect(bits, w, h, stride, btn_x, btn_y, btn_w, btn_h, btn_r, 255,
                   255, 255, 22);
    BlendRoundRectRing(bits, w, h, stride, btn_x + 0.6f, btn_y + 0.6f,
                       btn_w - 1.2f, btn_h - 1.2f,
                       (std::max)(1.0f, btn_r - 0.6f), 1.0f, 255, 255, 255, 28);
  }

  Gdiplus::Bitmap canvas(w, h, stride, PixelFormat32bppPARGB, bits);
  Gdiplus::Graphics g(&canvas);
  g.SetCompositingMode(Gdiplus::CompositingModeSourceOver);
  g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
  g.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHalf);
  std::wstring label = agent_count_ > 1
                           ? std::to_wstring(agent_count_) + L" Agents"
                           : L"Agent Controlled";
  Gdiplus::FontFamily family(L"Segoe UI");
  Gdiplus::SolidBrush text_brush(Gdiplus::Color(255, 240, 240, 240));
  const int btn_w_px = m.button.right - m.button.left;
  Gdiplus::RectF label_box(
      static_cast<Gdiplus::REAL>(DipToPx(16)), 0,
      static_cast<Gdiplus::REAL>(w - btn_w_px - DipToPx(20)),
      static_cast<Gdiplus::REAL>(h));
  FillAlignedText(&g, label.c_str(), family, Gdiplus::FontStyleRegular,
                  static_cast<Gdiplus::REAL>(DipToPx(13)), label_box,
                  text_brush, Gdiplus::StringAlignmentNear);
  Gdiplus::RectF brc(btn_x, btn_y, btn_w, btn_h);
  Gdiplus::SolidBrush btn_text(hover_button_
                                   ? Gdiplus::Color(255, 255, 157, 157)
                                   : Gdiplus::Color(255, 245, 245, 245));
  FillAlignedText(&g, L"Take Control", family, Gdiplus::FontStyleBold,
                  static_cast<Gdiplus::REAL>(DipToPx(12)), brc, btn_text,
                  Gdiplus::StringAlignmentCenter);
}

void AiHudOverlay::EnsureCursorCache() {
  const int size = SnapCursorSize(DipToPx(32));
  if (cursor_dib_ && cursor_dib_w_ == size && cursor_dib_h_ == size) {
    return;
  }
  if (cursor_dib_) {
    DeleteObject(cursor_dib_);
    cursor_dib_ = nullptr;
    cursor_bits_ = nullptr;
  }
  cursor_dib_ = CreatePargbDib(size, size, &cursor_bits_);
  cursor_dib_w_ = size;
  cursor_dib_h_ = size;
  cursor_hot_x_ = 0;
  cursor_hot_y_ = 0;
  if (!cursor_dib_ || !cursor_bits_) {
    return;
  }

  bool owned = false;
  HCURSOR cursor = LoadSystemArrowCursor(size, &owned);
  ICONINFO info = {};
  if (cursor && GetIconInfo(cursor, &info)) {
    BITMAP bm = {};
    if (info.hbmColor && GetObjectW(info.hbmColor, sizeof(bm), &bm) &&
        bm.bmWidth > 0 && bm.bmHeight > 0) {
      cursor_hot_x_ = info.xHotspot * size / bm.bmWidth;
      cursor_hot_y_ = info.yHotspot * size / bm.bmHeight;
    } else {
      cursor_hot_x_ = info.xHotspot;
      cursor_hot_y_ = info.yHotspot;
    }
    if (info.hbmMask) {
      DeleteObject(info.hbmMask);
    }
    if (info.hbmColor) {
      DeleteObject(info.hbmColor);
    }
  }

  HDC dc = CreateCompatibleDC(nullptr);
  if (dc) {
    HGDIOBJ old = SelectObject(dc, cursor_dib_);
    DrawIconEx(dc, 0, 0, cursor, size, size, 0, nullptr, DI_NORMAL);
    SelectObject(dc, old);
    DeleteDC(dc);
  }
  auto* px = static_cast<BYTE*>(cursor_bits_);
  bool needs_premul = false;
  for (int i = 0; i < size * size; ++i) {
    const BYTE* p = px + i * 4;
    if (p[0] > p[3] || p[1] > p[3] || p[2] > p[3]) {
      needs_premul = true;
      break;
    }
  }
  if (needs_premul) {
    PremultiplyArgb(cursor_bits_, size, size, size * 4);
  }
  if (owned && cursor) {
    DestroyCursor(cursor);
  }
}

void AiHudOverlay::BlitSprite(int dx, int dy, HBITMAP src, int sw, int sh) {
  if (!bits_ || !src) {
    return;
  }
  DIBSECTION ds = {};
  if (GetObjectW(src, sizeof(ds), &ds) == 0 || !ds.dsBm.bmBits) {
    return;
  }
  BlitPremul(static_cast<BYTE*>(bits_), dib_w_, dib_h_, dib_stride_, dx, dy,
             static_cast<const BYTE*>(ds.dsBm.bmBits), sw, sh,
             ds.dsBm.bmWidthBytes);
}

RECT AiHudOverlay::PointerDirtyRect(float x, float y) const {
  RECT rc = {};
  const int pad = DipToPx(40);
  rc.left = static_cast<int>(x) - pad;
  rc.top = static_cast<int>(y) - pad;
  rc.right = static_cast<int>(x) + pad + cursor_dib_w_;
  rc.bottom = static_cast<int>(y) + pad + cursor_dib_h_;
  return rc;
}

void AiHudOverlay::Present(const RECT* dirty) {
  if (!hwnd_ || !mem_dc_ || !dib_) {
    return;
  }
  HDC screen = GetDC(nullptr);
  POINT pt_src = {0, 0};
  SIZE size = {dib_w_, dib_h_};
  BLENDFUNCTION blend = {AC_SRC_OVER, 0, 255, AC_SRC_ALPHA};
  UPDATELAYEREDWINDOWINFO info = {};
  info.cbSize = sizeof(info);
  info.hdcDst = screen;
  info.psize = &size;
  info.hdcSrc = mem_dc_;
  info.pptSrc = &pt_src;
  info.pblend = &blend;
  info.dwFlags = ULW_ALPHA;
  RECT clip = {};
  if (dirty) {
    clip = *dirty;
    if (clip.left < 0) {
      clip.left = 0;
    }
    if (clip.top < 0) {
      clip.top = 0;
    }
    if (clip.right > dib_w_) {
      clip.right = dib_w_;
    }
    if (clip.bottom > dib_h_) {
      clip.bottom = dib_h_;
    }
    if (clip.right > clip.left && clip.bottom > clip.top) {
      info.prcDirty = &clip;
    }
  }
  if (!UpdateLayeredWindowIndirect(hwnd_, &info)) {
    UpdateLayeredWindow(hwnd_, screen, nullptr, &size, mem_dc_, &pt_src, 0,
                        &blend, ULW_ALPHA);
  }
  ReleaseDC(nullptr, screen);
}

void AiHudOverlay::Paint() {
  if (!hwnd_ || !active_) {
    return;
  }
  const Metrics m = ComputeMetrics();
  if (m.width < 8 || m.height < 8) {
    return;
  }
  if (cursor_shown_ && cursor_x_ < 0.0f) {
    cursor_x_ = static_cast<float>(m.width) * 0.55f;
    cursor_y_ = static_cast<float>(m.height) * 0.38f;
    cursor_to_x_ = cursor_x_;
    cursor_to_y_ = cursor_y_;
  }
  UpdatePointer(GetTickCount64());
  EnsureDib(m.width, m.height);
  if (!bits_) {
    return;
  }

  const int depth = DipToPx(42);
  const float corner = (parent_ && IsZoomed(parent_))
                           ? 0.0f
                           : static_cast<float>(DipToPx(8));
  const bool rebuilt =
      glow_w_ != m.width || glow_h_ != m.height || glow_depth_ != depth ||
      glow_corner_ != corner;
  RebuildGlowCache(m.width, m.height, depth, corner);

  RECT cursor_rc = PointerDirtyRect(cursor_x_, cursor_y_);
  ClearRect(last_cursor_rc_);
  ClearRect(cursor_rc);

  const double phase =
      (GetTickCount64() - start_tick_) / 2400.0 * 6.283185307179586;
  const float pulse =
      0.72f + 0.28f * static_cast<float>(std::sin(phase));
  PaintGlow(pulse);

  EnsurePillCache(m);
  if (pill_dib_) {
    BlitSprite(m.pill.left, m.pill.top, pill_dib_, pill_w_, pill_h_);
  }

  float click_t = 0.0f;
  if (click_pulse_start_ != 0) {
    const ULONGLONG nowp = GetTickCount64();
    if (nowp >= click_pulse_start_) {
      click_t = static_cast<float>(nowp - click_pulse_start_) / 280.0f;
      if (click_t >= 1.0f) {
        click_pulse_start_ = 0;
        click_t = 0.0f;
      }
    }
  }
  if (cursor_shown_ && cursor_x_ >= 0.0f) {
    const float dip = static_cast<float>(DipToPx(16)) / 16.0f;
    DrawPulseRing(static_cast<BYTE*>(bits_), dib_w_, dib_h_, dib_stride_,
                  cursor_x_, cursor_y_, click_t, dip);
    EnsureCursorCache();
    if (cursor_dib_) {
      BlitSprite(static_cast<int>(cursor_x_) - cursor_hot_x_,
                 static_cast<int>(cursor_y_) - cursor_hot_y_, cursor_dib_,
                 cursor_dib_w_, cursor_dib_h_);
    }
  }

  RECT dirty = {};
  SetRect(&dirty, 0, 0, depth + 3, m.height);
  RECT r = {m.width - depth - 3, 0, m.width, m.height};
  UnionRect(&dirty, &dirty, &r);
  SetRect(&r, 0, 0, m.width, depth + 3);
  UnionRect(&dirty, &dirty, &r);
  SetRect(&r, 0, m.height - depth - 3, m.width, m.height);
  UnionRect(&dirty, &dirty, &r);
  UnionRect(&dirty, &dirty, &m.pill);
  UnionRect(&dirty, &dirty, &last_cursor_rc_);
  UnionRect(&dirty, &dirty, &cursor_rc);
  last_cursor_rc_ = cursor_rc;

  Present(rebuilt ? nullptr : &dirty);
}

void AiHudOverlay::OnClick(POINT pt) {
  const Metrics m = ComputeMetrics();
  if (PointInRect(pt, m.button)) {
    McpServer::Get().PauseAgents();
  }
}

void AiHudOverlay::OnMouseMove(POINT pt) {
  const Metrics m = ComputeMetrics();
  const bool hover = PointInRect(pt, m.button);
  if (hover != hover_button_) {
    hover_button_ = hover;
    SetCursor(LoadCursor(nullptr, hover ? IDC_HAND : IDC_ARROW));
    Paint();
  }
}

LRESULT CALLBACK AiHudOverlay::WndProc(HWND hwnd, UINT msg, WPARAM wparam,
                                       LPARAM lparam) {
  auto* self =
      reinterpret_cast<AiHudOverlay*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
  if (msg == WM_NCCREATE) {
    auto* cs = reinterpret_cast<CREATESTRUCTW*>(lparam);
    SetWindowLongPtrW(hwnd, GWLP_USERDATA,
                      reinterpret_cast<LONG_PTR>(cs->lpCreateParams));
    return TRUE;
  }
  if (!self) {
    return DefWindowProcW(hwnd, msg, wparam, lparam);
  }
  switch (msg) {
    case WM_NCHITTEST: {
      POINT pt = {GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam)};
      ScreenToClient(hwnd, &pt);
      const Metrics m = self->ComputeMetrics();
      if (self->PointInRect(pt, m.button) || self->PointInRect(pt, m.pill)) {
        return HTCLIENT;
      }
      return HTTRANSPARENT;
    }
    case WM_SETCURSOR: {
      POINT pt = {};
      GetCursorPos(&pt);
      ScreenToClient(hwnd, &pt);
      const Metrics m = self->ComputeMetrics();
      SetCursor(LoadCursor(
          nullptr, self->PointInRect(pt, m.button) ? IDC_HAND : IDC_ARROW));
      return TRUE;
    }
    case WM_MOUSEMOVE: {
      POINT pt = {GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam)};
      self->OnMouseMove(pt);
      return 0;
    }
    case WM_LBUTTONUP: {
      POINT pt = {GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam)};
      self->OnClick(pt);
      return 0;
    }
    case WM_TIMER:
      if (self->active_) {
        self->Paint();
        self->SyncTimer();
      }
      return 0;
    default:
      return DefWindowProcW(hwnd, msg, wparam, lparam);
  }
}

}  // namespace omni

#else

namespace omni {

AiHudOverlay::AiHudOverlay() = default;
AiHudOverlay::~AiHudOverlay() = default;
void AiHudOverlay::Detach() {}
void AiHudOverlay::SetActive(bool, int) {}
void AiHudOverlay::Layout(int) {}
void AiHudOverlay::MovePointer(float, float, bool) {}

}  // namespace omni

#endif
