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

#include <folly/net/NetOps.h>
#include <folly/net/NetOpsDispatcher.h>

namespace folly {
namespace netops {
Dispatcher* Dispatcher::getDefaultInstance() {
  static Dispatcher wrapper = {};
  return &wrapper;
}

NetworkSocket Dispatcher::accept(
    NetworkSocket s, sockaddr* addr, socklen_t* addrlen) {
  return folly::netops::accept(s, addr, addrlen);
}

int Dispatcher::bind(NetworkSocket s, const sockaddr* name, socklen_t namelen) {
  return folly::netops::bind(s, name, namelen);
}

int Dispatcher::close(NetworkSocket s) {
  return folly::netops::close(s);
}

int Dispatcher::connect(
    NetworkSocket s, const sockaddr* name, socklen_t namelen) {
  return folly::netops::connect(s, name, namelen);
}

int Dispatcher::getpeername(
    NetworkSocket s, sockaddr* name, socklen_t* namelen) {
  return folly::netops::getpeername(s, name, namelen);
}

int Dispatcher::getsockname(
    NetworkSocket s, sockaddr* name, socklen_t* namelen) {
  return folly::netops::getsockname(s, name, namelen);
}

int Dispatcher::getsockopt(
    NetworkSocket s, int level, int optname, void* optval, socklen_t* optlen) {
  return folly::netops::getsockopt(s, level, optname, optval, optlen);
}

int Dispatcher::inet_aton(const char* cp, in_addr* inp) {
  return folly::netops::inet_aton(cp, inp);
}

int Dispatcher::listen(NetworkSocket s, int backlog) {
  return folly::netops::listen(s, backlog);
}

int Dispatcher::poll(PollDescriptor fds[], nfds_t nfds, int timeout) {
  return folly::netops::poll(fds, nfds, timeout);
}

ssize_t Dispatcher::recv(NetworkSocket s, void* buf, size_t len, int flags) {
  return folly::netops::recv(s, buf, len, flags);
}

ssize_t Dispatcher::recvfrom(
    NetworkSocket s,
    void* buf,
    size_t len,
    int flags,
    sockaddr* from,
    socklen_t* fromlen) {
  return folly::netops::recvfrom(s, buf, len, flags, from, fromlen);
}

ssize_t Dispatcher::recvmsg(NetworkSocket s, msghdr* message, int flags) {
  return folly::netops::recvmsg(s, message, flags);
}

int Dispatcher::recvmmsg(
    NetworkSocket s,
    mmsghdr* msgvec,
    unsigned int vlen,
    unsigned int flags,
    timespec* timeout) {
  return folly::netops::recvmmsg(s, msgvec, vlen, flags, timeout);
}

ssize_t Dispatcher::send(
    NetworkSocket s, const void* buf, size_t len, int flags) {
  return folly::netops::send(s, buf, len, flags);
}

ssize_t Dispatcher::sendmsg(
    NetworkSocket socket, const msghdr* message, int flags) {
  return folly::netops::sendmsg(socket, message, flags);
}

int Dispatcher::sendmmsg(
    NetworkSocket socket, mmsghdr* msgvec, unsigned int vlen, int flags) {
  return folly::netops::sendmmsg(socket, msgvec, vlen, flags);
}

ssize_t Dispatcher::sendto(
    NetworkSocket s,
    const void* buf,
    size_t len,
    int flags,
    const sockaddr* to,
    socklen_t tolen) {
  return folly::netops::sendto(s, buf, len, flags, to, tolen);
}

int Dispatcher::setsockopt(
    NetworkSocket s,
    int level,
    int optname,
    const void* optval,
    socklen_t optlen) {
  return folly::netops::setsockopt(s, level, optname, optval, optlen);
}

int Dispatcher::shutdown(NetworkSocket s, int how) {
  return folly::netops::shutdown(s, how);
}

NetworkSocket Dispatcher::socket(int af, int type, int protocol) {
  return folly::netops::socket(af, type, protocol);
}

int Dispatcher::socketpair(
    int domain, int type, int protocol, NetworkSocket sv[2]) {
  return folly::netops::socketpair(domain, type, protocol, sv);
}

int Dispatcher::set_socket_non_blocking(NetworkSocket s) {
  return folly::netops::set_socket_non_blocking(s);
}

int Dispatcher::set_socket_close_on_exec(NetworkSocket s) {
  return folly::netops::set_socket_close_on_exec(s);
}

} // namespace netops
} // namespace folly
