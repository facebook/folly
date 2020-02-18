/*
 * Copyright (c) Facebook, Inc. and its affiliates.
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

#pragma once

#include <folly/net/NetOps.h>

#include <folly/Portability.h>

namespace folly {
namespace portability {
namespace sockets {
#ifndef _WIN32
using ::accept;
using ::bind;
using ::connect;
using ::getpeername;
using ::getsockname;
using ::getsockopt;
using ::inet_ntop;
using ::listen;
using ::poll;
using ::recv;
using ::recvfrom;
using ::send;
using ::sendmsg;
using ::sendto;
using ::setsockopt;
using ::shutdown;
using ::socket;
#else
// Some Windows specific helper functions.
bool __cdecl is_fh_socket(int fh);
SOCKET __cdecl fd_to_socket(int fd);
int __cdecl socket_to_fd(SOCKET s);
int __cdecl translate_wsa_error(int wsaErr);

// These aren't additional overloads, but rather other functions that
// are referenced that we need to wrap, or, in the case of inet_aton,
// implement.
int __cdecl accept(int s, struct sockaddr* addr, socklen_t* addrlen);
int __cdecl inet_aton(const char* cp, struct in_addr* inp);
int __cdecl socketpair(int domain, int type, int protocol, int sv[2]);

// Unless you have a case where you would normally have
// to reference the function as being explicitly in the
// global scope, then you shouldn't be calling these directly.
int __cdecl bind(int s, const struct sockaddr* name, socklen_t namelen);
int __cdecl connect(int s, const struct sockaddr* name, socklen_t namelen);
int __cdecl getpeername(int s, struct sockaddr* name, socklen_t* namelen);
int __cdecl getsockname(int s, struct sockaddr* name, socklen_t* namelen);
int __cdecl getsockopt(int s, int level, int optname, void* optval, socklen_t* optlen);
const char* __cdecl inet_ntop(int af, const void* src, char* dst, socklen_t size);
int __cdecl listen(int s, int backlog);
int __cdecl poll(struct pollfd fds[], nfds_t nfds, int timeout);
ssize_t __cdecl recv(int s, void* buf, size_t len, int flags);
ssize_t __cdecl recvfrom(
    int s,
    void* buf,
    size_t len,
    int flags,
    struct sockaddr* from,
    socklen_t* fromlen);
ssize_t __cdecl send(int s, const void* buf, size_t len, int flags);
ssize_t __cdecl sendto(
    int s,
    const void* buf,
    size_t len,
    int flags,
    const sockaddr* to,
    socklen_t tolen);
ssize_t __cdecl sendmsg(int socket, const struct msghdr* message, int flags);
int __cdecl setsockopt(
    int s,
    int level,
    int optname,
    const void* optval,
    socklen_t optlen);
int __cdecl shutdown(int s, int how);

// This is the only function that _must_ be referenced via the namespace
// because there is no difference in parameter types to overload
// on.
int __cdecl socket(int af, int type, int protocol);

// Windows needs a few extra overloads of some of the functions in order to
// resolve to our portability functions rather than the SOCKET accepting
// ones.
int __cdecl getsockopt(int s, int level, int optname, char* optval, socklen_t* optlen);
ssize_t __cdecl recv(int s, char* buf, int len, int flags);
ssize_t __cdecl recv(int s, void* buf, int len, int flags);
ssize_t __cdecl recvfrom(
    int s,
    char* buf,
    int len,
    int flags,
    struct sockaddr* from,
    socklen_t* fromlen);
ssize_t __cdecl recvfrom(
    int s,
    void* buf,
    int len,
    int flags,
    struct sockaddr* from,
    socklen_t* fromlen);
ssize_t __cdecl recvmsg(int s, struct msghdr* message, int fl);
ssize_t __cdecl send(int s, const char* buf, int len, int flags);
ssize_t __cdecl send(int s, const void* buf, int len, int flags);
ssize_t __cdecl sendto(
    int s,
    const char* buf,
    int len,
    int flags,
    const sockaddr* to,
    socklen_t tolen);
ssize_t __cdecl sendto(
    int s,
    const void* buf,
    int len,
    int flags,
    const sockaddr* to,
    socklen_t tolen);
int __cdecl setsockopt(
    int s,
    int level,
    int optname,
    const char* optval,
    socklen_t optlen);
#endif
} // namespace sockets
} // namespace portability
} // namespace folly

#ifdef _WIN32
// Add our helpers to the overload set.
FOLLY_PUSH_WARNING
FOLLY_CLANG_DISABLE_WARNING("-Wheader-hygiene")
/* using override */
using namespace folly::portability::sockets;
FOLLY_POP_WARNING
#endif
