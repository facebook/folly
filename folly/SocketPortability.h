/*
 * Copyright 2015 Facebook, Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#pragma once

#ifdef _MSC_VER
# define HAVE_UNIX_SOCKETS 0
#else
# define HAVE_UNIX_SOCKETS 1
#endif

#ifdef _MSC_VER
#include <fcntl.h>
#ifndef __STDC__
#define __STDC__ 1
#include <io.h>
#undef __STDC__
#else
#include <io.h>
#endif
#include <WinSock2.h>
#include <WS2tcpip.h>
#include <ws2def.h>
#include <ws2ipdef.h>
#include <MSWSock.h>

#include <folly/Portability.h>
#include <folly/FilePortability.h>

inline int inet_aton(const char *cp, struct in_addr *inp) {
  inp->s_addr = inet_addr(cp);
  return inp->s_addr == INADDR_NONE ? 0 : 1;
}

namespace folly { namespace socket_portability {

  template<class R, class F, class... Args>
  R wrapSocketFunction(F f, int s, Args... args) {
    // We do this in a roundabout way to enable us to
    // do a bit of trickery to ensure that things aren't
    // being implicitly converted to a SOCKET by temporarily
    // adjusting the windows headers to define SOCKET as a
    // structure.
    HANDLE tmp = (HANDLE)_get_osfhandle(s);
    SOCKET h = *(SOCKET*)&tmp;

    return f(h, args...);
  }
  inline int accept(int s, struct sockaddr* addr, int* addrlen) {
    SOCKET ns = wrapSocketFunction<SOCKET>(::accept, s, addr, addrlen);
    return _open_osfhandle((intptr_t)ns, O_RDWR | O_BINARY);
  }
  inline int bind(int s, const struct sockaddr* name, int namelen) { return wrapSocketFunction<int>(::bind, s, name, namelen); }
  inline int connect(int s, const struct sockaddr* name, int namelen) { return wrapSocketFunction<int>(::connect, s, name, namelen); }
  inline int getpeername(int s, struct sockaddr* name, int* namelen) { return wrapSocketFunction<int>(::getpeername, s, name, namelen); }
  inline int getsockname(int s, struct sockaddr* name, int* namelen) { return wrapSocketFunction<int>(::getsockname, s, name, namelen); }
  inline int getsockopt(int s, int level, int optname, void* optval, int* optlen) { return wrapSocketFunction<int>(::getsockopt, s, level, optname, (char*)optval, optlen); }
  inline const char* inet_ntop(int af, const void* src, char* dst, socklen_t size) { return ::inet_ntop(af, (char*)src, dst, size); }
  inline int listen(int s, int backlog) { return wrapSocketFunction<int>(::listen, s, backlog); }
  struct pollfd {
    int fd;
    short events;
    short revents;
  };
  inline int poll(pollfd* fds, int nfds, int timeout) {
    ::pollfd* wfds = (::pollfd*)calloc(nfds, sizeof(::pollfd));
    for (int i = 0; i < nfds; i++) {
      wfds[i].fd = (SOCKET)_get_osfhandle(fds[i].fd);
      wfds[i].events = fds[i].events;
      wfds[i].revents = fds[i].revents;
    }
    int ret = ::WSAPoll(wfds, (ULONG)nfds, timeout);

    for (int i = 0; i < nfds; i++) {
      fds[i].events = wfds[i].events;
      fds[i].revents = wfds[i].revents;
    }
    free(wfds);

    return ret;
  }
  inline int recv(int s, void* buf, int len, int flags) { return wrapSocketFunction<int>(::recv, s, (char*)buf, len, flags); }
  inline int recvfrom(int s, void* buf, int len, int flags, struct sockaddr* from, int* fromlen) { return wrapSocketFunction<int>(::recvfrom, s, (char*)buf, len, flags, from, fromlen); }
  inline int send(int s, const void* buf, int len, int flags) { return wrapSocketFunction<int>(::send, s, (char*)buf, len, flags); }
  inline int sendto(int s, const void* buf, int len, int flags, const sockaddr* to, int tolen) { return wrapSocketFunction<int>(::sendto, s, (char*)buf, len, flags, to, tolen); }
  inline int setsockopt(int s, int level, int optname, const void* optval, int optlen) { return wrapSocketFunction<int>(::setsockopt, s, level, optname, (char*)optval, optlen); }
#define SHUT_RD SD_RECEIVE
#define SHUT_WR SD_SEND
#define SHUT_RDWR SD_BOTH
  inline int shutdown(int s, int how) { return wrapSocketFunction<int>(::shutdown, s, how); }
  inline int socket(int af, int type, int protocol) {
    SOCKET h = ::socket(af, type, protocol);
    return _open_osfhandle((intptr_t)h, O_RDWR | O_BINARY);
  }
  inline int socketpair(int domain, int type, int protocol, int sockot[2])
  {
    struct sockaddr_in address;
    SOCKET redirect;
    SOCKET sock[2];
    int size = sizeof(address);

    if (domain != AF_INET) {
      WSASetLastError(WSAENOPROTOOPT);
      return -1;
    }

    sock[0] = sock[1] = redirect = INVALID_SOCKET;


    sock[0] = ::socket(domain, type, protocol);
    if (INVALID_SOCKET == sock[0]) {
      goto error;
    }

    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_family = AF_INET;
    address.sin_port = 0;

    if (::bind(sock[0], (struct sockaddr*)&address, sizeof(address)) != 0) {
      goto error;
    }

    if (::getsockname(sock[0], (struct sockaddr *)&address, &size) != 0) {
      goto error;
    }

    if (::listen(sock[0], 2) != 0) {
      goto error;
    }

    sock[1] = ::socket(domain, type, protocol);
    if (INVALID_SOCKET == sock[1]) {
      goto error;
    }

    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (::connect(sock[1], (struct sockaddr*)&address, sizeof(address)) != 0) {
      goto error;
    }

    redirect = ::accept(sock[0], (struct sockaddr*)&address, &size);
    if (INVALID_SOCKET == redirect) {
      goto error;
    }

    ::closesocket(sock[0]);
    sock[0] = redirect;

    sockot[0] = _open_osfhandle((intptr_t)sock[0], O_RDWR | O_BINARY);
    sockot[1] = _open_osfhandle((intptr_t)sock[1], O_RDWR | O_BINARY);
    return 0;

  error:
    ::closesocket(redirect);
    ::closesocket(sock[0]);
    ::closesocket(sock[1]);
    ::WSASetLastError(WSAECONNABORTED);
    return -1;
  }

// We don't actually support either of these flags
// currently.
#define MSG_DONTWAIT 1
#define MSG_EOR 0
  struct msghdr
  {
    void* msg_name;
    socklen_t msg_namelen;
    struct iovec* msg_iov;
    size_t msg_iovlen;
    void* msg_control;
    size_t msg_controllen;
    int msg_flags;
  };

  inline ssize_t sendmsg(int s, const struct msghdr *message, int fl) {
    HANDLE tmp = (HANDLE)_get_osfhandle(s);
    SOCKET h = *(SOCKET*)&tmp;

    // Don't currently support the name translation.
    if (message->msg_name != nullptr || message->msg_namelen != 0)
      return (ssize_t)-1;
    WSAMSG msg;
    msg.name = nullptr;
    msg.namelen = 0;
    msg.Control.buf = (CHAR*)message->msg_control;
    msg.Control.len = (ULONG)message->msg_controllen;
    msg.dwFlags = 0;
    msg.dwBufferCount = (DWORD)message->msg_iovlen;
    msg.lpBuffers = new WSABUF[message->msg_iovlen];
    for (size_t i = 0; i < message->msg_iovlen; i++) {
      msg.lpBuffers[i].buf = (CHAR*)message->msg_iov[i].iov_base;
      msg.lpBuffers[i].len = (ULONG)message->msg_iov[i].iov_len;
    }

    DWORD bytesSent;
    int res = WSASendMsg(h, &msg, 0, &bytesSent, nullptr, nullptr);
    delete[] msg.lpBuffers;
    if (res == 0)
      return (ssize_t)bytesSent;
    return -1;
  }

  inline ssize_t recvmsg(int s, struct msghdr* message, int fl) {
    HANDLE tmp = (HANDLE)_get_osfhandle(s);
    SOCKET h = *(SOCKET*)&tmp;

    // Don't currently support the name translation.
    if (message->msg_name != nullptr || message->msg_namelen != 0)
      return (ssize_t)-1;
    WSAMSG msg;
    msg.name = nullptr;
    msg.namelen = 0;
    msg.Control.buf = (CHAR*)message->msg_control;
    msg.Control.len = (ULONG)message->msg_controllen;
    msg.dwFlags = 0;
    msg.dwBufferCount = (DWORD)message->msg_iovlen;
    msg.lpBuffers = new WSABUF[message->msg_iovlen];
    for (size_t i = 0; i < message->msg_iovlen; i++) {
      msg.lpBuffers[i].buf = (CHAR*)message->msg_iov[i].iov_base;
      msg.lpBuffers[i].len = (ULONG)message->msg_iov[i].iov_len;
    }

    LPFN_WSARECVMSG WSARecvMsg;
    GUID WSARecgMsg_GUID = WSAID_WSARECVMSG;
    DWORD recMsgBytes;
    WSAIoctl(h, SIO_GET_EXTENSION_FUNCTION_POINTER,
      &WSARecgMsg_GUID, sizeof(WSARecgMsg_GUID),
      &WSARecvMsg, sizeof(WSARecvMsg),
      &recMsgBytes, nullptr, nullptr);

    DWORD bytesReceived;
    int res = WSARecvMsg(h, &msg, &bytesReceived, nullptr, nullptr);
    delete[] msg.lpBuffers;
    if (res == 0)
      return (ssize_t)bytesReceived;
    return -1;
  }

}}

// And a few things that have different names in posix.
#define sa_family_t ADDRESS_FAMILY
#undef s_host

#else
#include <netdb.h>
#include <poll.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#include <sys/un.h>

namespace folly { namespace socket_portability {
  inline int bind(int s, const struct sockaddr* name, int namelen) { return ::bind(s, name, namelen); }
  inline int connect(int s, const struct sockaddr* name, int namelen) { return ::connect(s, name, namelen); }
  inline int getpeername(int s, struct sockaddr* name, int* namelen) { return ::getpeername(s, name, namelen); }
  inline int getsockname(int s, struct sockaddr* name, int* namelen) { return ::getsockname(s, name, namelen); }
  inline int getsockopt(int s, int level, int optname, void* optval, int* optlen) { return ::getsockopt(s, level, optname, optval, optlen); }
  inline const char* inet_ntop(int af, const void* src, char* dst, socklen_t size) { return ::inet_ntop(af, src, dst, size); }
  inline int listen(int s, int backlog) { return ::listen(s, backlog); }
  typedef ::pollfd pollfd;
  inline int poll(struct pollfd fds[], nfds_t nfds, int timeout) { return ::poll(fds, nfds, timeout); }
  inline int recv(int s, void* buf, int len, int flags) { return ::recv(s, buf, len, flags); }
  inline int recvfrom(int s, void* buf, int len, int flags, struct sockaddr* from, int* fromlen) { return ::recvfrom(s, buf, len, flags, from, fromlen); }
  inline int send(int s, const void* buf, int len, int flags) { return ::send(s, buf, len, flags); }
  inline int sendto(int s, const void* buf, int len, int flags, const sockaddr* to, int tolen) { return ::sendto(s, buf, len, flags, to, tolen); }
  typedef ::msghdr msghdr;
  inline ssize_t sendmsg(int socket, const struct msghdr* message, int flags) { return ::sendmsg(socket, message, flags); }
  inline int setsockopt(int s, int level, int optname, const void* optval, int optlen) { return ::setsockopt(s, level, optname, optval, optlen); }
  inline int shutdown(int s, int how) { return ::shutdown(s, how); }
  inline int socket(int af, int type, int protocol) { return ::socket(af, type, protocol); }
  inline int socketpair(int domain, int type, int protocol, int sockot[2]) { return ::socketpair(domain, type, protocol, sockot); }
}}
#endif

namespace fsp = folly::socket_portability;
