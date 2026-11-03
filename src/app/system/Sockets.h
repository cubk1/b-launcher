#pragma once

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cerrno>

using SOCKET = int;
using u_short = unsigned short;
using BOOL = int;
using DWORD = unsigned int;

constexpr SOCKET INVALID_SOCKET = -1;
constexpr int SOCKET_ERROR = -1;
constexpr BOOL TRUE = 1;
constexpr int WSAEINTR = EINTR;
constexpr int WSAEWOULDBLOCK = EWOULDBLOCK;
constexpr int WSAEAFNOSUPPORT = EAFNOSUPPORT;
constexpr int WSAEADDRNOTAVAIL = EADDRNOTAVAIL;

inline int closesocket(SOCKET socket) {
    return close(socket);
}

inline int WSAGetLastError() {
    return errno;
}
#endif
