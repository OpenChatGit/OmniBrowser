#pragma once

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <cerrno>
#include <netinet/in.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

using SOCKET = int;
constexpr SOCKET INVALID_SOCKET = -1;
constexpr int SOCKET_ERROR = -1;

inline int closesocket(SOCKET s) {
  return ::close(s);
}

inline int WSAGetLastError() {
  return errno;
}
#endif

namespace omni {

inline bool NetStartup() {
#if defined(_WIN32)
  WSADATA wsa;
  return WSAStartup(MAKEWORD(2, 2), &wsa) == 0;
#else
  return true;
#endif
}

inline void NetCleanup() {
#if defined(_WIN32)
  WSACleanup();
#endif
}

inline int SelectRead(SOCKET s, timeval* tv) {
#if defined(_WIN32)
  fd_set read_set;
  FD_ZERO(&read_set);
  FD_SET(s, &read_set);
  return select(0, &read_set, nullptr, nullptr, tv);
#else
  fd_set read_set;
  FD_ZERO(&read_set);
  FD_SET(s, &read_set);
  return select(static_cast<int>(s) + 1, &read_set, nullptr, nullptr, tv);
#endif
}

}  // namespace omni
