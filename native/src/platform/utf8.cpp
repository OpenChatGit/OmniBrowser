#include "omni/utf8.h"

#if defined(_WIN32)
#include <windows.h>
#else
#include <cstdint>
#endif

namespace omni::utf8 {

#if defined(_WIN32)

std::string Narrow(const std::wstring& wide) {
  if (wide.empty()) {
    return {};
  }
  const int size = WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), -1, nullptr, 0,
                                       nullptr, nullptr);
  std::string out(size > 0 ? size - 1 : 0, '\0');
  if (size > 1) {
    WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), -1, out.data(), size, nullptr,
                        nullptr);
  }
  return out;
}

std::wstring Widen(const std::string& utf8) {
  if (utf8.empty()) {
    return {};
  }
  const int size =
      MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), -1, nullptr, 0);
  std::wstring out(size > 0 ? size - 1 : 0, L'\0');
  if (size > 1) {
    MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), -1, out.data(), size);
  }
  return out;
}

#else

std::string Narrow(const std::wstring& wide) {
  std::string out;
  out.reserve(wide.size() * 3);
  for (wchar_t wc : wide) {
    const uint32_t cp = static_cast<uint32_t>(wc);
    if (cp < 0x80) {
      out.push_back(static_cast<char>(cp));
    } else if (cp < 0x800) {
      out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
      out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else if (cp < 0x10000) {
      out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
      out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
      out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else {
      out.push_back(static_cast<char>(0xF0 | (cp >> 18)));
      out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
      out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
      out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    }
  }
  return out;
}

std::wstring Widen(const std::string& utf8) {
  std::wstring out;
  out.reserve(utf8.size());
  size_t i = 0;
  while (i < utf8.size()) {
    const unsigned char c = static_cast<unsigned char>(utf8[i]);
    uint32_t cp = 0;
    int extra = 0;
    if (c < 0x80) {
      cp = c;
      extra = 0;
    } else if ((c >> 5) == 0x6) {
      cp = c & 0x1F;
      extra = 1;
    } else if ((c >> 4) == 0xE) {
      cp = c & 0x0F;
      extra = 2;
    } else if ((c >> 3) == 0x1E) {
      cp = c & 0x07;
      extra = 3;
    } else {
      ++i;
      continue;
    }
    ++i;
    bool ok = true;
    for (int j = 0; j < extra; ++j) {
      if (i >= utf8.size()) {
        ok = false;
        break;
      }
      const unsigned char cc = static_cast<unsigned char>(utf8[i]);
      if ((cc >> 6) != 0x2) {
        ok = false;
        break;
      }
      cp = (cp << 6) | (cc & 0x3F);
      ++i;
    }
    if (ok) {
      out.push_back(static_cast<wchar_t>(cp));
    }
  }
  return out;
}

#endif

}  // namespace omni::utf8
