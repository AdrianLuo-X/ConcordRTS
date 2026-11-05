#pragma once

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <cerrno>
#include <netinet/in.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

namespace concord::test::loopback {

#ifdef _WIN32
using NativeSocket = SOCKET;
using NativeSocketLength = int;
constexpr NativeSocket invalidSocket = INVALID_SOCKET;
constexpr int shutdownWrite = SD_SEND;
constexpr int sendFlags = 0;

inline void closeSocket(NativeSocket socket) { ::closesocket(socket); }
inline int lastSocketError() { return WSAGetLastError(); }
inline int selectWidth(NativeSocket) { return 0; }

inline bool setReceiveTimeout(NativeSocket socket) {
    const DWORD milliseconds = 1000;
    return ::setsockopt(socket, SOL_SOCKET, SO_RCVTIMEO,
        reinterpret_cast<const char*>(&milliseconds), sizeof(milliseconds)) == 0;
}
#else
using NativeSocket = int;
using NativeSocketLength = socklen_t;
constexpr NativeSocket invalidSocket = -1;
constexpr int shutdownWrite = SHUT_WR;
constexpr int sendFlags = MSG_NOSIGNAL;

inline void closeSocket(NativeSocket socket) { ::close(socket); }
inline int lastSocketError() { return errno; }
inline int selectWidth(NativeSocket socket) { return socket + 1; }

inline bool setReceiveTimeout(NativeSocket socket) {
    const timeval timeout{1, 0};
    return ::setsockopt(socket, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)) == 0;
}
#endif

class SocketSystem {
public:
    SocketSystem() {
#ifdef _WIN32
        WSADATA data{};
        ready = WSAStartup(MAKEWORD(2, 2), &data) == 0;
#else
        const NativeSocket socket = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        ready = socket != invalidSocket;
        if (ready) closeSocket(socket);
#endif
    }

    ~SocketSystem() {
#ifdef _WIN32
        if (ready) WSACleanup();
#endif
    }

    SocketSystem(const SocketSystem&) = delete;
    SocketSystem& operator=(const SocketSystem&) = delete;

    bool ready = false;
};

} // namespace concord::test::loopback
