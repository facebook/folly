/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <folly/net/NetOps.h>

#include <fcntl.h>
#include <cerrno>

#ifdef __EMSCRIPTEN__
#include <cassert>
#endif

#include <cstddef>
#include <stdexcept>

#include <folly/CPortability.h>
#include <folly/ScopeGuard.h>
#include <folly/Utility.h>
#include <folly/net/detail/SocketFileDescriptorMap.h>

#ifdef _WIN32
#include <MSWSock.h> // @manual
#endif

#if (defined(__linux__) && !defined(__ANDROID__)) ||                       \
    (defined(__ANDROID__) && __ANDROID_API__ >= 21 /* released 2014 */) || \
    defined(__FreeBSD__) || defined(__SGX__) || defined(__EMSCRIPTEN__)
static_assert(folly::to_bool(::recvmmsg));
static_assert(folly::to_bool(::sendmmsg));
#else
static int (*recvmmsg)(...) = nullptr;
static int (*sendmmsg)(...) = nullptr;
#endif

namespace folly {
namespace netops {

namespace {
#ifdef _WIN32
// WSA has to be explicitly initialized.
static struct WinSockInit {
  WinSockInit() {
    WSADATA dat;
    WSAStartup(MAKEWORD(2, 2), &dat);
  }
  ~WinSockInit() { WSACleanup(); }
} winsockInit;

static int wsa_error_translator_base(
    NetworkSocket, intptr_t, intptr_t, int wsaErr) {
  switch (wsaErr) {
    case WSAEWOULDBLOCK:
      return EAGAIN;
    default:
      return wsaErr;
  }
}

wsa_error_translator_ptr wsa_error_translator = wsa_error_translator_base;

#define translate_wsa_error(e, s, f, r) \
  wsa_error_translator(                 \
      s, reinterpret_cast<intptr_t>(f), static_cast<intptr_t>(r), e)

#endif

template <class R, class F, class... Args>
static R wrapSocketFunction(F f, NetworkSocket s, Args... args) {
  R ret = f(s.data, args...);
#ifdef _WIN32
  errno = translate_wsa_error(WSAGetLastError(), s, f, ret);
#endif
  return ret;
}
} // namespace

#ifdef _WIN32
void set_wsa_error_translator(
    wsa_error_translator_ptr translator,
    wsa_error_translator_ptr* previousOut) {
  // Do an atomic swap of the error translator, ensuring we've filled in the
  // previous one first so they can be safely daisy changed/called, e.g.
  // translator may want to call the previous one and a call to it may come in
  // even before we return.
  PVOID result = nullptr;
  do {
    *previousOut = wsa_error_translator;
    result = InterlockedCompareExchangePointer(
        reinterpret_cast<PVOID volatile*>(&wsa_error_translator),
        reinterpret_cast<PVOID>(translator),
        reinterpret_cast<PVOID>(*previousOut));
  } while (result != reinterpret_cast<PVOID>(*previousOut));
}
#endif

NetworkSocket accept(NetworkSocket s, sockaddr* addr, socklen_t* addrlen) {
#if defined(__EMSCRIPTEN__)
  throw std::logic_error("Not implemented!");
#else
  return NetworkSocket(wrapSocketFunction<NetworkSocket::native_handle_type>(
      ::accept, s, addr, addrlen));
#endif
}

int bind(NetworkSocket s, const sockaddr* name, socklen_t namelen) {
#if defined(__EMSCRIPTEN__)
  throw std::logic_error("Not implemented!");
#else
  if (kIsWindows && name->sa_family == AF_UNIX) {
    // Windows added support for AF_UNIX sockets, but didn't add
    // support for autobind sockets, so detect requests for autobind
    // sockets and treat them as invalid. (otherwise they don't trigger
    // an error, but also don't actually work)
    if (name->sa_data[0] == '\0') {
      errno = EINVAL;
      return -1;
    }
  }
  return wrapSocketFunction<int>(::bind, s, name, namelen);
#endif
}

int close(NetworkSocket s) {
#if defined(__EMSCRIPTEN__)
  throw std::logic_error("Not implemented!");
#else
  return netops::detail::SocketFileDescriptorMap::close(s.data);
#endif
}

int connect(NetworkSocket s, const sockaddr* name, socklen_t namelen) {
#if defined(__EMSCRIPTEN__)
  throw std::logic_error("Not implemented!");
#else
  auto r = wrapSocketFunction<int>(::connect, s, name, namelen);
#ifdef _WIN32
  if (r == -1 && WSAGetLastError() == WSAEWOULDBLOCK) {
    errno = EINPROGRESS;
  }
#endif
  return r;
#endif
}

int getpeername(NetworkSocket s, sockaddr* name, socklen_t* namelen) {
#if defined(__EMSCRIPTEN__)
  throw std::logic_error("Not implemented!");
#else
  return wrapSocketFunction<int>(::getpeername, s, name, namelen);
#endif
}

int getsockname(NetworkSocket s, sockaddr* name, socklen_t* namelen) {
#if defined(__EMSCRIPTEN__)
  throw std::logic_error("Not implemented!");
#else
  return wrapSocketFunction<int>(::getsockname, s, name, namelen);
#endif
}

int getsockopt(
    NetworkSocket s, int level, int optname, void* optval, socklen_t* optlen) {
#if defined(__EMSCRIPTEN__)
  throw std::logic_error("Not implemented!");
#else
  auto ret = wrapSocketFunction<int>(
      ::getsockopt, s, level, optname, (char*)optval, optlen);
#ifdef _WIN32
  if (optname == TCP_NODELAY && *optlen == 1) {
    // Windows is weird about this value, and documents it as a
    // BOOL (ie. int) but expects the variable to be bool (1-byte),
    // so we get to adapt the interface to work that way.
    *(int*)optval = *(uint8_t*)optval;
    *optlen = sizeof(int);
  }
#endif
  return ret;
#endif
}

int inet_aton(const char* cp, in_addr* inp) {
#if defined(__EMSCRIPTEN__)
  throw std::logic_error("Not implemented!");
#else
  inp->s_addr = inet_addr(cp);
  return inp->s_addr == INADDR_NONE ? 0 : 1;
#endif
}

int listen(NetworkSocket s, int backlog) {
#if defined(__EMSCRIPTEN__)
  throw std::logic_error("Not implemented!");
#else
  return wrapSocketFunction<int>(::listen, s, backlog);
#endif
}

int poll(PollDescriptor fds[], nfds_t nfds, int timeout) {
#if defined(__EMSCRIPTEN__)
  throw std::logic_error("Not implemented!");
#else
  // Make sure that PollDescriptor is byte-for-byte identical to pollfd,
  // so we don't need extra allocations just for the safety of this shim.
  static_assert(
      alignof(PollDescriptor) == alignof(pollfd),
      "PollDescriptor is misaligned");
  static_assert(
      sizeof(PollDescriptor) == sizeof(pollfd),
      "PollDescriptor is the wrong size");
  static_assert(
      offsetof(PollDescriptor, fd) == offsetof(pollfd, fd),
      "PollDescriptor.fd is at the wrong place");
  static_assert(
      sizeof(decltype(PollDescriptor().fd)) == sizeof(decltype(pollfd().fd)),
      "PollDescriptor.fd is the wrong size");
  static_assert(
      offsetof(PollDescriptor, events) == offsetof(pollfd, events),
      "PollDescriptor.events is at the wrong place");
  static_assert(
      sizeof(decltype(PollDescriptor().events)) ==
          sizeof(decltype(pollfd().events)),
      "PollDescriptor.events is the wrong size");
  static_assert(
      offsetof(PollDescriptor, revents) == offsetof(pollfd, revents),
      "PollDescriptor.revents is at the wrong place");
  static_assert(
      sizeof(decltype(PollDescriptor().revents)) ==
          sizeof(decltype(pollfd().revents)),
      "PollDescriptor.revents is the wrong size");

  // Pun it through
  auto files = reinterpret_cast<pollfd*>(reinterpret_cast<void*>(fds));
#ifdef _WIN32
  return ::WSAPoll(files, (ULONG)nfds, timeout);
#else
  return ::poll(files, nfds, timeout);
#endif
#endif // defined(__EMSCRIPTEN__)
}

ssize_t recv(NetworkSocket s, void* buf, size_t len, int flags) {
#ifdef _WIN32
  if ((flags & MSG_DONTWAIT) == MSG_DONTWAIT) {
    flags &= ~MSG_DONTWAIT;

    u_long pendingRead = 0;
    if (ioctlsocket(s.data, FIONREAD, &pendingRead)) {
      errno = translate_wsa_error(WSAGetLastError(), s, ::ioctlsocket, -1);
      return -1;
    }

    fd_set readSet;
    FD_ZERO(&readSet);
    FD_SET(s.data, &readSet);
    timeval timeout{0, 0};
    auto ret = select(1, &readSet, nullptr, nullptr, &timeout);
    if (ret == 0) {
      errno = EWOULDBLOCK;
      return -1;
    }
  }
  return wrapSocketFunction<ssize_t>(::recv, s, (char*)buf, (int)len, flags);
#elif defined(__EMSCRIPTEN__)
  throw std::logic_error("Not implemented!");
#else
  return wrapSocketFunction<ssize_t>(::recv, s, buf, len, flags);
#endif
}

#ifdef _WIN32
ssize_t wsaRecvMesg(NetworkSocket s, WSAMSG* wsaMsg) {
  SOCKET h = s.data;

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

  // Attempt to disable ICMP behavior which kills the socket.
  BOOL connReset = false;
  DWORD bytesReturned = 0;
  WSAIoctl(
      h,
      SIO_UDP_CONNRESET,
      &connReset,
      sizeof(connReset),
      nullptr,
      0,
      &bytesReturned,
      nullptr,
      nullptr);

  DWORD bytesReceived;
  int res = WSARecvMsg(h, wsaMsg, &bytesReceived, nullptr, nullptr);
  errno = translate_wsa_error(WSAGetLastError(), s, WSARecvMsg, res);

  if (res == 0) {
    return bytesReceived;
  }
  if ((wsaMsg->dwFlags & MSG_TRUNC) == MSG_TRUNC) {
    return wsaMsg->lpBuffers[0].len + 1;
  }
  return -1;
}
#endif

ssize_t recvfrom(
    NetworkSocket s,
    void* buf,
    size_t len,
    int flags,
    sockaddr* from,
    socklen_t* fromlen) {
#ifdef _WIN32
  if ((flags & MSG_TRUNC) == MSG_TRUNC) {
    WSABUF wBuf{};
    wBuf.buf = (CHAR*)buf;
    wBuf.len = (ULONG)len;
    WSAMSG wMsg{};
    wMsg.dwBufferCount = 1;
    wMsg.lpBuffers = &wBuf;
    wMsg.name = from;
    if (fromlen != nullptr) {
      wMsg.namelen = *fromlen;
    }

    return wsaRecvMesg(s, &wMsg);
  }
  return wrapSocketFunction<ssize_t>(
      ::recvfrom, s, (char*)buf, (int)len, flags, from, fromlen);
#elif defined(__EMSCRIPTEN__)
  throw std::logic_error("Not implemented!");
#else
  return wrapSocketFunction<ssize_t>(
      ::recvfrom, s, buf, len, flags, from, fromlen);
#endif
}

ssize_t recvmsg(NetworkSocket s, msghdr* message, int flags) {
#ifdef _WIN32
  (void)flags;

  WSAMSG msg;
  msg.name = (LPSOCKADDR)message->msg_name;
  msg.namelen = message->msg_namelen;
  msg.Control.buf = (CHAR*)message->msg_control;
  msg.Control.len = (ULONG)message->msg_controllen;
  msg.dwFlags = 0;
  msg.dwBufferCount = (DWORD)message->msg_iovlen;
  msg.lpBuffers = new WSABUF[message->msg_iovlen];
  SCOPE_EXIT {
    delete[] msg.lpBuffers;
  };
  for (size_t i = 0; i < message->msg_iovlen; i++) {
    msg.lpBuffers[i].buf = (CHAR*)message->msg_iov[i].iov_base;
    msg.lpBuffers[i].len = (ULONG)message->msg_iov[i].iov_len;
  }

  return wsaRecvMesg(s, &msg);
#elif defined(__EMSCRIPTEN__)
  throw std::logic_error("Not implemented!");
#else
  return wrapSocketFunction<ssize_t>(::recvmsg, s, message, flags);
#endif
}

int recvmmsg(
    NetworkSocket s,
    mmsghdr* msgvec,
    unsigned int vlen,
    unsigned int flags,
    timespec* timeout) {
#if defined(__EMSCRIPTEN__)
  throw std::logic_error("Not implemented!");
#else
  if (to_bool(::recvmmsg)) {
    return wrapSocketFunction<int>(::recvmmsg, s, msgvec, vlen, flags, timeout);
  }
  // implement via recvmsg
  FOLLY_PUSH_WARNING
  FOLLY_CLANG_DISABLE_WARNING("-Wunreachable-code")
  for (unsigned int i = 0; i < vlen; i++) {
    ssize_t ret = recvmsg(s, &msgvec[i].msg_hdr, flags);
    // in case of an error
    // we return the number of msgs received if > 0
    // or an error if no msg was sent
    if (ret < 0) {
      if (i) {
        return static_cast<int>(i);
      }
      return static_cast<int>(ret);
    } else {
      msgvec[i].msg_len = ret;
    }
  }
  return static_cast<int>(vlen);
  FOLLY_POP_WARNING
#endif
}

ssize_t send(NetworkSocket s, const void* buf, size_t len, int flags) {
#ifdef _WIN32
  return wrapSocketFunction<ssize_t>(
      ::send, s, (const char*)buf, (int)len, flags);
#elif defined(__EMSCRIPTEN__)
  throw std::logic_error("Not implemented!");
#else
  return wrapSocketFunction<ssize_t>(::send, s, buf, len, flags);
#endif
}

[[maybe_unused]] static ssize_t fakeSendmsg(
    [[maybe_unused]] NetworkSocket socket,
    [[maybe_unused]] const msghdr* message) {
#ifdef _WIN32
  SOCKET h = socket.data;
  ssize_t bytesSent = 0;
  for (size_t i = 0; i < message->msg_iovlen; i++) {
    int r = -1;
    if (message->msg_name != nullptr) {
      r = ::sendto(
          h,
          (const char*)message->msg_iov[i].iov_base,
          (int)message->msg_iov[i].iov_len,
          message->msg_flags,
          (const sockaddr*)message->msg_name,
          (int)message->msg_namelen);
    } else {
      r = ::send(
          h,
          (const char*)message->msg_iov[i].iov_base,
          (int)message->msg_iov[i].iov_len,
          message->msg_flags);
    }
    if (r == -1 || size_t(r) != message->msg_iov[i].iov_len) {
      errno = translate_wsa_error(WSAGetLastError(), socket, fakeSendmsg, r);
      if (WSAGetLastError() == WSAEWOULDBLOCK && bytesSent > 0) {
        return bytesSent;
      }
      return -1;
    }
    bytesSent += r;
  }
  return bytesSent;
#else
  throw std::logic_error("Not implemented!");
#endif
}

#ifdef _WIN32
[[maybe_unused]] ssize_t wsaSendMsgDirect(
    [[maybe_unused]] NetworkSocket socket, [[maybe_unused]] WSAMSG* msg) {
  // WSASendMsg freaks out if this pointer is not set to null but length is 0.
  if (msg->Control.len == 0) {
    msg->Control.buf = nullptr;
  }
  SOCKET h = socket.data;
  DWORD bytesSent;
  auto ret = WSASendMsg(h, msg, 0, &bytesSent, nullptr, nullptr);
  errno = translate_wsa_error(WSAGetLastError(), socket, WSASendMsg, ret);
  return ret == 0 ? (ssize_t)bytesSent : -1;
}
#endif

[[maybe_unused]] static ssize_t wsaSendMsg(
    [[maybe_unused]] NetworkSocket socket,
    [[maybe_unused]] const msghdr* message,
    [[maybe_unused]] int flags) {
#ifdef _WIN32
  // Translate msghdr to WSAMSG.
  WSAMSG msg;
  msg.name = (LPSOCKADDR)message->msg_name;
  msg.namelen = message->msg_namelen;
  msg.Control.buf = (CHAR*)message->msg_control;
  msg.Control.len = (ULONG)message->msg_controllen;
  msg.dwFlags = flags;
  msg.dwBufferCount = (DWORD)message->msg_iovlen;
  msg.lpBuffers = new WSABUF[message->msg_iovlen];
  SCOPE_EXIT {
    delete[] msg.lpBuffers;
  };
  for (size_t i = 0; i < message->msg_iovlen; i++) {
    msg.lpBuffers[i].buf = (CHAR*)message->msg_iov[i].iov_base;
    msg.lpBuffers[i].len = (ULONG)message->msg_iov[i].iov_len;
  }
  return wsaSendMsgDirect(socket, &msg);
#else
  throw std::logic_error("Not implemented!");
#endif
}

ssize_t sendmsg(NetworkSocket socket, const msghdr* message, int flags) {
#ifdef _WIN32

  // Check socket type to see if WSASendMsg usage is a go.
  DWORD socketType = 0;
  auto len = sizeof(socketType);
  auto ret =
      getsockopt(socket, SOL_SOCKET, SO_TYPE, &socketType, (socklen_t*)&len);
  if (ret != 0) {
    errno = translate_wsa_error(WSAGetLastError(), socket, getsockopt, ret);
    return ret;
  }

  if (socketType == SOCK_DGRAM || socketType == SOCK_RAW) {
    return wsaSendMsg(socket, message, flags);
  } else {
    // Unfortunately, WSASendMsg requires the socket to have been opened
    // as either SOCK_DGRAM or SOCK_RAW, but sendmsg has no such requirement,
    // so we have to implement it based on send instead :(
    return fakeSendmsg(socket, message);
  }
#elif defined(__EMSCRIPTEN__)
  throw std::logic_error("Not implemented!");
#else
  return wrapSocketFunction<ssize_t>(::sendmsg, socket, message, flags);
#endif
}

int sendmmsg(
    NetworkSocket socket, mmsghdr* msgvec, unsigned int vlen, int flags) {
#if defined(__EMSCRIPTEN__)
  throw std::logic_error("Not implemented!");
#else
  if (to_bool(::sendmmsg)) {
    return wrapSocketFunction<int>(::sendmmsg, socket, msgvec, vlen, flags);
  }
  // implement via sendmsg
  FOLLY_PUSH_WARNING
  FOLLY_CLANG_DISABLE_WARNING("-Wunreachable-code")
  for (unsigned int i = 0; i < vlen; i++) {
    ssize_t ret = sendmsg(socket, &msgvec[i].msg_hdr, flags);
    // in case of an error
    // we return the number of msgs sent if > 0
    // or an error if no msg was sent
    if (ret < 0) {
      if (i) {
        return static_cast<int>(i);
      }

      return static_cast<int>(ret);
    } else {
      msgvec[i].msg_len = ret;
    }
  }

  return static_cast<int>(vlen);
  FOLLY_POP_WARNING
#endif
}

ssize_t sendto(
    NetworkSocket s,
    const void* buf,
    size_t len,
    int flags,
    const sockaddr* to,
    socklen_t tolen) {
#ifdef _WIN32
  return wrapSocketFunction<ssize_t>(
      ::sendto, s, (const char*)buf, (int)len, flags, to, (int)tolen);
#elif defined(__EMSCRIPTEN__)
  throw std::logic_error("Not implemented!");
#else
  return wrapSocketFunction<ssize_t>(::sendto, s, buf, len, flags, to, tolen);
#endif
}

int setsockopt(
    NetworkSocket s,
    int level,
    int optname,
    const void* optval,
    socklen_t optlen) {
#ifdef _WIN32
  return wrapSocketFunction<int>(
      ::setsockopt, s, level, optname, (char*)optval, optlen);
#elif defined(__EMSCRIPTEN__)
  throw std::logic_error("Not implemented!");
#else
  return wrapSocketFunction<int>(
      ::setsockopt, s, level, optname, optval, optlen);
#endif
}

int shutdown(NetworkSocket s, int how) {
#if defined(__EMSCRIPTEN__)
  throw std::logic_error("Not implemented!");
#else
  return wrapSocketFunction<int>(::shutdown, s, how);
#endif
}

NetworkSocket socket(int af, int type, int protocol) {
#if defined(__EMSCRIPTEN__)
  throw std::logic_error("Not implemented!");
#else
  return NetworkSocket(::socket(af, type, protocol));
#endif
}

#ifdef _WIN32

//  adapted from like code in libevent, itself adapted from like code in tor
//
//  from: https://github.com/libevent/libevent/tree/release-2.1.12-stable
//  license: 3-Clause BSD
static int socketpair_win32(
    int family, int type, int protocol, intptr_t fd[2]) {
  intptr_t listener = -1;
  intptr_t connector = -1;
  intptr_t acceptor = -1;
  struct sockaddr_in listen_addr;
  struct sockaddr_in connect_addr;
  int size;
  int saved_errno = -1;
  int family_test;

  family_test = family != AF_INET && (family != AF_UNIX);
  if (protocol || family_test) {
    WSASetLastError(WSAEAFNOSUPPORT);
    return -1;
  }

  if (!fd) {
    WSASetLastError(WSAEINVAL);
    return -1;
  }

  listener = ::socket(AF_INET, type, 0);
  if (listener < 0) {
    return -1;
  }
  memset(&listen_addr, 0, sizeof(listen_addr));
  listen_addr.sin_family = AF_INET;
  listen_addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  listen_addr.sin_port = 0; /* kernel chooses port.   */
  if (::bind(listener, (struct sockaddr*)&listen_addr, sizeof(listen_addr)) ==
      -1) {
    goto tidy_up_and_fail;
  }
  if (::listen(listener, 1) == -1) {
    goto tidy_up_and_fail;
  }

  connector = ::socket(AF_INET, type, 0);
  if (connector < 0) {
    goto tidy_up_and_fail;
  }

  memset(&connect_addr, 0, sizeof(connect_addr));

  /* We want to find out the port number to connect to.  */
  size = sizeof(connect_addr);
  if (::getsockname(listener, (struct sockaddr*)&connect_addr, &size) == -1) {
    goto tidy_up_and_fail;
  }
  if (size != sizeof(connect_addr)) {
    goto abort_tidy_up_and_fail;
  }
  if (::connect(
          connector, (struct sockaddr*)&connect_addr, sizeof(connect_addr)) ==
      -1) {
    goto tidy_up_and_fail;
  }

  size = sizeof(listen_addr);
  acceptor = ::accept(listener, (struct sockaddr*)&listen_addr, &size);
  if (acceptor < 0) {
    goto tidy_up_and_fail;
  }
  if (size != sizeof(listen_addr)) {
    goto abort_tidy_up_and_fail;
  }
  /* Now check we are talking to ourself by matching port and host on the
     two sockets.   */
  if (::getsockname(connector, (struct sockaddr*)&connect_addr, &size) == -1) {
    goto tidy_up_and_fail;
  }
  if (size != sizeof(connect_addr) ||
      listen_addr.sin_family != connect_addr.sin_family ||
      listen_addr.sin_addr.s_addr != connect_addr.sin_addr.s_addr ||
      listen_addr.sin_port != connect_addr.sin_port) {
    goto abort_tidy_up_and_fail;
  }
  ::closesocket(listener);
  fd[0] = connector;
  fd[1] = acceptor;

  return 0;

abort_tidy_up_and_fail:
  saved_errno = WSAECONNABORTED;

tidy_up_and_fail:
  if (saved_errno < 0) {
    saved_errno = WSAGetLastError();
  }
  if (listener != -1) {
    ::closesocket(listener);
  }
  if (connector != -1) {
    ::closesocket(connector);
  }
  if (acceptor != -1) {
    ::closesocket(acceptor);
  }

  WSASetLastError(saved_errno);
  return -1;
}

#endif

int socketpair(int domain, int type, int protocol, NetworkSocket sv[2]) {
#ifdef _WIN32
  if (domain != PF_UNIX || type != SOCK_STREAM || protocol != 0) {
    return -1;
  }
  intptr_t pair[2];
  auto r = socketpair_win32(AF_INET, type, protocol, pair);
  if (r == -1) {
    return r;
  }
  sv[0] = NetworkSocket(static_cast<SOCKET>(pair[0]));
  sv[1] = NetworkSocket(static_cast<SOCKET>(pair[1]));
  return r;
#elif defined(__EMSCRIPTEN__)
  throw std::logic_error("Not implemented!");
#else
  int pair[2];
  auto r = ::socketpair(domain, type, protocol, pair);
  if (r == -1) {
    return r;
  }
  sv[0] = NetworkSocket(pair[0]);
  sv[1] = NetworkSocket(pair[1]);
  return r;
#endif
}

int set_socket_non_blocking(NetworkSocket s) {
#ifdef _WIN32
  u_long nonBlockingEnabled = 1;
  return ioctlsocket(s.data, FIONBIO, &nonBlockingEnabled);
#elif defined(__EMSCRIPTEN__)
  throw std::logic_error("Not implemented!");
#else
  int flags = fcntl(s.data, F_GETFL, 0);
  if (flags == -1) {
    return -1;
  }
  return fcntl(s.data, F_SETFL, flags | O_NONBLOCK);
#endif
}

int set_socket_close_on_exec(NetworkSocket s) {
#ifdef _WIN32
  if (SetHandleInformation((HANDLE)s.data, HANDLE_FLAG_INHERIT, 0)) {
    return 0;
  }
  return -1;
#elif defined(__EMSCRIPTEN__)
  throw std::logic_error("Not implemented!");
#else
  return fcntl(s.data, F_SETFD, FD_CLOEXEC);
#endif
}

void Msgheader::setName(sockaddr_storage* addrStorage, size_t len) {
  FOLLY_PUSH_WARNING
  FOLLY_CLANG_DISABLE_WARNING("-Wundef")
#ifdef _WIN32
  msg_.name = reinterpret_cast<LPSOCKADDR>(addrStorage);
  msg_.namelen = len;
#elif __EMSCRIPTEN__
  assert(false); // not supported in emcc
#else
  msg_.msg_name = reinterpret_cast<void*>(addrStorage);
  msg_.msg_namelen = len;
#endif
  FOLLY_POP_WARNING
}

void Msgheader::setIovecs(const struct iovec* vec, size_t iovec_len) {
#ifdef _WIN32
  msg_.dwBufferCount = (DWORD)iovec_len;
  wsaBufs_.reset(new WSABUF[iovec_len]);
  msg_.lpBuffers = wsaBufs_.get();
  for (size_t i = 0; i < iovec_len; i++) {
    msg_.lpBuffers[i].buf = (CHAR*)vec[i].iov_base;
    msg_.lpBuffers[i].len = (ULONG)vec[i].iov_len;
  }
#else
  msg_.msg_iov = const_cast<struct iovec*>(vec);
  msg_.msg_iovlen = iovec_len;
#endif
}

void Msgheader::setCmsgPtr(char* ctrlBuf) {
#ifdef _WIN32
  msg_.Control.buf = ctrlBuf;
#else
  msg_.msg_control = ctrlBuf;
#endif
}

void Msgheader::setCmsgLen(size_t len) {
#ifdef _WIN32
  msg_.Control.len = len;
#else
  msg_.msg_controllen = len;
#endif
}

void Msgheader::setFlags(int flags) {
#ifdef _WIN32
  msg_.dwFlags = flags;
#else
  msg_.msg_flags = flags;
#endif
}

void Msgheader::incrCmsgLen(size_t val) {
  FOLLY_PUSH_WARNING
  FOLLY_CLANG_DISABLE_WARNING("-Wundef")
#ifdef _WIN32
  msg_.Control.len += WSA_CMSG_SPACE(val);
#elif __EMSCRIPTEN__
  assert(false); // not supported in emcc
#else
  msg_.msg_controllen += CMSG_SPACE(val);
#endif
  FOLLY_POP_WARNING
}

XPLAT_CMSGHDR* Msgheader::getFirstOrNextCmsgHeader(XPLAT_CMSGHDR* cm) {
  return cm ? cmsgNextHrd(cm) : cmsgFirstHrd();
}

XPLAT_MSGHDR* Msgheader::getMsg() {
  return &msg_;
}

XPLAT_CMSGHDR* Msgheader::cmsgNextHrd(XPLAT_CMSGHDR* cm) {
  FOLLY_PUSH_WARNING
  FOLLY_CLANG_DISABLE_WARNING("-Wundef")
#ifdef _WIN32
  return WSA_CMSG_NXTHDR(&msg_, cm);
#elif __EMSCRIPTEN__
  assert(false); // not supported in emcc
#else
  return CMSG_NXTHDR(&msg_, cm);
#endif
  FOLLY_POP_WARNING
}

XPLAT_CMSGHDR* Msgheader::cmsgFirstHrd() {
  FOLLY_PUSH_WARNING
  FOLLY_CLANG_DISABLE_WARNING("-Wundef")
#ifdef _WIN32
  return WSA_CMSG_FIRSTHDR(&msg_);
#elif __EMSCRIPTEN__
  assert(false); // not supported in emcc
#else
  return CMSG_FIRSTHDR(&msg_);
#endif
  FOLLY_POP_WARNING
}
} // namespace netops
} // namespace folly
