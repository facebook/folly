/*
 * Copyright 2016 Facebook, Inc.
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

#include <folly/portability/Sockets.h>

#ifdef _MSC_VER

#include <errno.h>
#include <fcntl.h>

#include <MSWSock.h>

#include <folly/ScopeGuard.h>

namespace folly {
namespace portability {
namespace sockets {

// We have to startup WSA.
static struct FSPInit {
  FSPInit() {
    WSADATA dat;
    WSAStartup(MAKEWORD(2, 2), &dat);
  }
  ~FSPInit() { WSACleanup(); }
} fspInit;

bool is_fh_socket(int fh) {
  SOCKET h = fd_to_socket(fh);
  constexpr long kDummyEvents = 0xABCDEF12;
  WSANETWORKEVENTS e;
  e.lNetworkEvents = kDummyEvents;
  WSAEnumNetworkEvents(h, nullptr, &e);
  return e.lNetworkEvents != kDummyEvents;
}

SOCKET fd_to_socket(int fd) {
  // We do this in a roundabout way to allow us to compile even if
  // we're doing a bit of trickery to ensure that things aren't
  // being implicitly converted to a SOCKET by temporarily
  // adjusting the windows headers to define SOCKET as a
  // structure.
  static_assert(sizeof(HANDLE) == sizeof(SOCKET), "Handle size mismatch.");
  HANDLE tmp = (HANDLE)_get_osfhandle(fd);
  return *(SOCKET*)&tmp;
}

int socket_to_fd(SOCKET s) {
  return _open_osfhandle((intptr_t)s, O_RDWR | O_BINARY);
}

template <class R, class F, class... Args>
static R wrapSocketFunction(F f, int s, Args... args) {
  SOCKET h = fd_to_socket(s);
  R ret = f(h, args...);
  errno = WSAGetLastError();
  return ret;
}

int accept(int s, struct sockaddr* addr, socklen_t* addrlen) {
  return socket_to_fd(wrapSocketFunction<SOCKET>(::accept, s, addr, addrlen));
}

int bind(int s, const struct sockaddr* name, socklen_t namelen) {
  return wrapSocketFunction<int>(::bind, s, name, namelen);
}

int connect(int s, const struct sockaddr* name, socklen_t namelen) {
  return wrapSocketFunction<int>(::connect, s, name, namelen);
}

int getpeername(int s, struct sockaddr* name, socklen_t* namelen) {
  return wrapSocketFunction<int>(::getpeername, s, name, namelen);
}

int getsockname(int s, struct sockaddr* name, socklen_t* namelen) {
  return wrapSocketFunction<int>(::getsockname, s, name, namelen);
}

int getsockopt(int s, int level, int optname, char* optval, socklen_t* optlen) {
  return wrapSocketFunction<int>(
      ::getsockopt, s, level, optname, (char*)optval, optlen);
}

int getsockopt(int s, int level, int optname, void* optval, socklen_t* optlen) {
  return wrapSocketFunction<int>(
      ::getsockopt, s, level, optname, (char*)optval, optlen);
}

int inet_aton(const char* cp, struct in_addr* inp) {
  inp->s_addr = inet_addr(cp);
  return inp->s_addr == INADDR_NONE ? 0 : 1;
}

const char* inet_ntop(int af, const void* src, char* dst, socklen_t size) {
  return ::inet_ntop(af, (char*)src, dst, size);
}

int listen(int s, int backlog) {
  return wrapSocketFunction<int>(::listen, s, backlog);
}

int poll(struct pollfd fds[], nfds_t nfds, int timeout) {
  // TODO: Allow both file descriptors and SOCKETs in this.
  for (int i = 0; i < nfds; i++) {
    fds[i].fd = fd_to_socket(fds[i].fd);
  }
  return ::WSAPoll(fds, (ULONG)nfds, timeout);
}

ssize_t recv(int s, void* buf, size_t len, int flags) {
  return wrapSocketFunction<ssize_t>(::recv, s, (char*)buf, (int)len, flags);
}

ssize_t recv(int s, char* buf, int len, int flags) {
  return wrapSocketFunction<ssize_t>(::recv, s, (char*)buf, len, flags);
}

ssize_t recv(int s, void* buf, int len, int flags) {
  return wrapSocketFunction<ssize_t>(::recv, s, (char*)buf, len, flags);
}

ssize_t recvfrom(
    int s,
    void* buf,
    size_t len,
    int flags,
    struct sockaddr* from,
    socklen_t* fromlen) {
  return wrapSocketFunction<ssize_t>(
      ::recvfrom, s, (char*)buf, (int)len, flags, from, fromlen);
}

ssize_t recvfrom(
    int s,
    char* buf,
    int len,
    int flags,
    struct sockaddr* from,
    socklen_t* fromlen) {
  return wrapSocketFunction<ssize_t>(
      ::recvfrom, s, (char*)buf, len, flags, from, fromlen);
}

ssize_t recvfrom(
    int s,
    void* buf,
    int len,
    int flags,
    struct sockaddr* from,
    socklen_t* fromlen) {
  return wrapSocketFunction<ssize_t>(
      ::recvfrom, s, (char*)buf, len, flags, from, fromlen);
}

ssize_t recvmsg(int s, struct msghdr* message, int fl) {
  SOCKET h = fd_to_socket(s);

  // Don't currently support the name translation.
  if (message->msg_name != nullptr || message->msg_namelen != 0) {
    return (ssize_t)-1;
  }
  WSAMSG msg;
  msg.name = nullptr;
  msg.namelen = 0;
  msg.Control.buf = (CHAR*)message->msg_control;
  msg.Control.len = (ULONG)message->msg_controllen;
  msg.dwFlags = 0;
  msg.dwBufferCount = (DWORD)message->msg_iovlen;
  msg.lpBuffers = new WSABUF[message->msg_iovlen];
  SCOPE_EXIT { delete[] msg.lpBuffers; };
  for (size_t i = 0; i < message->msg_iovlen; i++) {
    msg.lpBuffers[i].buf = (CHAR*)message->msg_iov[i].iov_base;
    msg.lpBuffers[i].len = (ULONG)message->msg_iov[i].iov_len;
  }

  // WSARecvMsg is an extension, so we don't get
  // the convenience of being able to call it directly, even though
  // WSASendMsg is part of the normal API -_-...
  LPFN_WSARECVMSG WSARecvMsg;
  GUID WSARecgMsg_GUID = WSAID_WSARECVMSG;
  DWORD recMsgBytes;
  WSAIoctl(
      h,
      SIO_GET_EXTENSION_FUNCTION_POINTER,
      &WSARecgMsg_GUID,
      sizeof(WSARecgMsg_GUID),
      &WSARecvMsg,
      sizeof(WSARecvMsg),
      &recMsgBytes,
      nullptr,
      nullptr);

  DWORD bytesReceived;
  int res = WSARecvMsg(h, &msg, &bytesReceived, nullptr, nullptr);
  return res == 0 ? (ssize_t)bytesReceived : -1;
}

ssize_t send(int s, const void* buf, size_t len, int flags) {
  return wrapSocketFunction<ssize_t>(::send, s, (char*)buf, (int)len, flags);
}

ssize_t send(int s, const char* buf, int len, int flags) {
  return wrapSocketFunction<ssize_t>(::send, s, (char*)buf, len, flags);
}

ssize_t send(int s, const void* buf, int len, int flags) {
  return wrapSocketFunction<ssize_t>(::send, s, (char*)buf, len, flags);
}

ssize_t sendmsg(int s, const struct msghdr* message, int fl) {
  SOCKET h = fd_to_socket(s);

  // Don't currently support the name translation.
  if (message->msg_name != nullptr || message->msg_namelen != 0) {
    return (ssize_t)-1;
  }
  WSAMSG msg;
  msg.name = nullptr;
  msg.namelen = 0;
  msg.Control.buf = (CHAR*)message->msg_control;
  msg.Control.len = (ULONG)message->msg_controllen;
  msg.dwFlags = 0;
  msg.dwBufferCount = (DWORD)message->msg_iovlen;
  msg.lpBuffers = new WSABUF[message->msg_iovlen];
  SCOPE_EXIT { delete[] msg.lpBuffers; };
  for (size_t i = 0; i < message->msg_iovlen; i++) {
    msg.lpBuffers[i].buf = (CHAR*)message->msg_iov[i].iov_base;
    msg.lpBuffers[i].len = (ULONG)message->msg_iov[i].iov_len;
  }

  DWORD bytesSent;
  int res = WSASendMsg(h, &msg, 0, &bytesSent, nullptr, nullptr);
  return res == 0 ? (ssize_t)bytesSent : -1;
}

ssize_t sendto(
    int s,
    const void* buf,
    size_t len,
    int flags,
    const sockaddr* to,
    socklen_t tolen) {
  return wrapSocketFunction<ssize_t>(
      ::sendto, s, (char*)buf, (int)len, flags, to, tolen);
}

ssize_t sendto(
    int s,
    const char* buf,
    int len,
    int flags,
    const sockaddr* to,
    socklen_t tolen) {
  return wrapSocketFunction<ssize_t>(
      ::sendto, s, (char*)buf, len, flags, to, tolen);
}

ssize_t sendto(
    int s,
    const void* buf,
    int len,
    int flags,
    const sockaddr* to,
    socklen_t tolen) {
  return wrapSocketFunction<ssize_t>(
      ::sendto, s, (char*)buf, len, flags, to, tolen);
}

int setsockopt(
    int s,
    int level,
    int optname,
    const char* optval,
    socklen_t optlen) {
  return wrapSocketFunction<int>(
      ::setsockopt, s, level, optname, (char*)optval, optlen);
}

int setsockopt(
    int s,
    int level,
    int optname,
    const void* optval,
    socklen_t optlen) {
  return wrapSocketFunction<int>(
      ::setsockopt, s, level, optname, (char*)optval, optlen);
}

int shutdown(int s, int how) {
  return wrapSocketFunction<int>(::shutdown, s, how);
}

int socket(int af, int type, int protocol) {
  return socket_to_fd(::socket(af, type, protocol));
}
}
}
}
#endif
