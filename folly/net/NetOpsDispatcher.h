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

#include <memory>

#include <folly/net/NetOps.h>

namespace folly {
namespace netops {

/**
 * Dispatcher for netops:: calls.
 *
 * Using a Dispatcher instead of calling netops:: directly enables tests to
 * mock netops:: calls.
 */
class Dispatcher {
 public:
  static Dispatcher* getDefaultInstance();

  virtual NetworkSocket accept(
      NetworkSocket s, sockaddr* addr, socklen_t* addrlen);
  virtual int bind(NetworkSocket s, const sockaddr* name, socklen_t namelen);
  virtual int close(NetworkSocket s);
  virtual int connect(NetworkSocket s, const sockaddr* name, socklen_t namelen);
  virtual int getpeername(NetworkSocket s, sockaddr* name, socklen_t* namelen);
  virtual int getsockname(NetworkSocket s, sockaddr* name, socklen_t* namelen);
  virtual int getsockopt(
      NetworkSocket s, int level, int optname, void* optval, socklen_t* optlen);
  virtual int inet_aton(const char* cp, in_addr* inp);
  virtual int listen(NetworkSocket s, int backlog);
  virtual int poll(PollDescriptor fds[], nfds_t nfds, int timeout);
  virtual ssize_t recv(NetworkSocket s, void* buf, size_t len, int flags);
  virtual ssize_t recvfrom(
      NetworkSocket s,
      void* buf,
      size_t len,
      int flags,
      sockaddr* from,
      socklen_t* fromlen);
  virtual ssize_t recvmsg(NetworkSocket s, msghdr* message, int flags);
  virtual int recvmmsg(
      NetworkSocket s,
      mmsghdr* msgvec,
      unsigned int vlen,
      unsigned int flags,
      timespec* timeout);
  virtual ssize_t send(NetworkSocket s, const void* buf, size_t len, int flags);
  virtual ssize_t sendto(
      NetworkSocket s,
      const void* buf,
      size_t len,
      int flags,
      const sockaddr* to,
      socklen_t tolen);
  virtual ssize_t sendmsg(
      NetworkSocket socket, const msghdr* message, int flags);
  virtual int sendmmsg(
      NetworkSocket socket, mmsghdr* msgvec, unsigned int vlen, int flags);
  virtual int setsockopt(
      NetworkSocket s,
      int level,
      int optname,
      const void* optval,
      socklen_t optlen);
  virtual int shutdown(NetworkSocket s, int how);
  virtual NetworkSocket socket(int af, int type, int protocol);
  virtual int socketpair(
      int domain, int type, int protocol, NetworkSocket sv[2]);

  virtual int set_socket_non_blocking(NetworkSocket s);
  virtual int set_socket_close_on_exec(NetworkSocket s);

 protected:
  Dispatcher() = default;
  virtual ~Dispatcher() = default;
};

/**
 * Container for netops::Dispatcher.
 *
 * Enables override Dispatcher to be installed for tests and special cases.
 * If no override installed, returns default Dispatcher instance.
 */
class DispatcherContainer {
 public:
  /**
   * Returns Dispatcher.
   *
   * If no override installed, returns default Dispatcher instance.
   */
  netops::Dispatcher* getDispatcher() const {
    return overrideDispatcher_ ? overrideDispatcher_.get()
                               : Dispatcher::getDefaultInstance();
  }

  /**
   * Returns Dispatcher.
   *
   * If no override installed, returns default Dispatcher instance.
   */
  netops::Dispatcher* operator->() const { return getDispatcher(); }

  /**
   * Sets override Dispatcher. To remove override, pass empty shared_ptr.
   */
  void setOverride(std::shared_ptr<netops::Dispatcher> dispatcher) {
    overrideDispatcher_ = std::move(dispatcher);
  }

  /**
   * If installed, returns shared_ptr to override Dispatcher, else empty ptr.
   */
  std::shared_ptr<netops::Dispatcher> getOverride() const {
    return overrideDispatcher_;
  }

 private:
  std::shared_ptr<netops::Dispatcher> overrideDispatcher_;
};

} // namespace netops
} // namespace folly
