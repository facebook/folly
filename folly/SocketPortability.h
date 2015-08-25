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

// While MSVC may not have Unix sockets, Cygwin does.
#ifdef _MSC_VER
# define HAVE_UNIX_SOCKETS 0
#else
# define HAVE_UNIX_SOCKETS 1
#endif

#ifdef _MSC_VER

#ifndef __STDC__
#define __STDC__ 1
#include <io.h>
#undef __STDC__
#else
#include <io.h>
#endif

#include <fcntl.h>
#include <WinSock2.h>
#include <WS2tcpip.h>
#include <ws2def.h>
#include <ws2ipdef.h>
#include <MSWSock.h>

#include <folly/Portability.h>
#include <folly/FilePortability.h>

 // We don't actually support either of these flags
 // currently.
#define MSG_DONTWAIT 1
#define MSG_EOR 0
struct msghdr {
  void* msg_name;
  socklen_t msg_namelen;
  struct iovec* msg_iov;
  size_t msg_iovlen;
  void* msg_control;
  size_t msg_controllen;
  int msg_flags;
};

#define SHUT_RD SD_RECEIVE
#define SHUT_WR SD_SEND
#define SHUT_RDWR SD_BOTH

namespace folly { namespace socket_portability {
  SOCKET fd_to_socket(int fd);
  int socket_to_fd(SOCKET s);

  // Unless you have a case where you would normally have
  // to reference the function as being explicitly in the
  // global scope, then you shouldn't be calling these directly.
  int accept(int s, struct sockaddr* addr, int* addrlen);
  int bind(int s, const struct sockaddr* name, int namelen);
  int connect(int s, const struct sockaddr* name, int namelen);
  int getpeername(int s, struct sockaddr* name, int* namelen);
  int getsockname(int s, struct sockaddr* name, int* namelen);
  int getsockopt(int s, int level, int optname, char* optval, int* optlen);
  int getsockopt(int s, int level, int optname, void* optval, int* optlen);
  int inet_aton(const char *cp, struct in_addr *inp);
  const char* inet_ntop(int af, const void* src, char* dst, socklen_t size);
  int listen(int s, int backlog);
  int poll(pollfd* fds, int nfds, int timeout);
  int recv(int s, char* buf, int len, int flags);
  int recv(int s, void* buf, int len, int flags);
  int recvfrom(int s, char* buf, int len, int flags, struct sockaddr* from, int* fromlen);
  int recvfrom(int s, void* buf, int len, int flags, struct sockaddr* from, int* fromlen);
  ssize_t recvmsg(int s, struct msghdr* message, int fl);
  int send(int s, const char* buf, int len, int flags);
  int send(int s, const void* buf, int len, int flags);
  ssize_t sendmsg(int s, const struct msghdr *message, int fl);
  int sendto(int s, const char* buf, int len, int flags, const sockaddr* to, int tolen);
  int sendto(int s, const void* buf, int len, int flags, const sockaddr* to, int tolen);
  // We use this first overload to convince MSVC to use our overloads, rather than the
  // winsock ones, when optval is a const char*.
  int setsockopt(int s, int level, int optname, const char* optval, int optlen);
  int setsockopt(int s, int level, int optname, const void* optval, int optlen);
  int shutdown(int s, int how);
  int socketpair(int domain, int type, int protocol, int sockot[2]);

  // This is the only function that _must_ be referenced via fsp::
  // because there is no difference in parameter types to overload
  // on.
  int socket(int af, int type, int protocol);
}}

using namespace folly::socket_portability;

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
  // Unless you have a case where you would normally have
  // to reference the function as being explicitly in the
  // global scope, then you shouldn't be calling these directly.
  inline int bind(int s, const struct sockaddr* name, int namelen) { return ::bind(s, name, namelen); }
  inline int connect(int s, const struct sockaddr* name, int namelen) { return ::connect(s, name, namelen); }
  inline int getpeername(int s, struct sockaddr* name, int* namelen) { return ::getpeername(s, name, namelen); }
  inline int getsockname(int s, struct sockaddr* name, int* namelen) { return ::getsockname(s, name, namelen); }
  inline int getsockopt(int s, int level, int optname, void* optval, int* optlen) { return ::getsockopt(s, level, optname, optval, optlen); }
  inline const char* inet_ntop(int af, const void* src, char* dst, socklen_t size) { return ::inet_ntop(af, src, dst, size); }
  inline int listen(int s, int backlog) { return ::listen(s, backlog); }
  inline int poll(struct pollfd fds[], nfds_t nfds, int timeout) { return ::poll(fds, nfds, timeout); }
  inline int recv(int s, void* buf, int len, int flags) { return ::recv(s, buf, len, flags); }
  inline int recvfrom(int s, void* buf, int len, int flags, struct sockaddr* from, int* fromlen) { return ::recvfrom(s, buf, len, flags, from, fromlen); }
  inline int send(int s, const void* buf, int len, int flags) { return ::send(s, buf, len, flags); }
  inline int sendto(int s, const void* buf, int len, int flags, const sockaddr* to, int tolen) { return ::sendto(s, buf, len, flags, to, tolen); }
  inline ssize_t sendmsg(int socket, const struct msghdr* message, int flags) { return ::sendmsg(socket, message, flags); }
  inline int setsockopt(int s, int level, int optname, const void* optval, int optlen) { return ::setsockopt(s, level, optname, optval, optlen); }
  inline int shutdown(int s, int how) { return ::shutdown(s, how); }
  inline int socketpair(int domain, int type, int protocol, int sockot[2]) { return ::socketpair(domain, type, protocol, sockot); }

  // This is the only function that _must_ be referenced via fsp::
  // because there is no difference in parameter types to overload
  // on.
  inline int socket(int af, int type, int protocol) { return ::socket(af, type, protocol); }
}}
#endif

namespace fsp = folly::socket_portability;
