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

#pragma once

#include <system_error>

#include <folly/Expected.h>
#include <folly/net/NetOpsDispatcher.h>
#include <folly/net/TcpInfo.h>

namespace folly {

/**
 * Dispatcher that enables calls to TcpInfo to be intercepted for tests.
 */
class TcpInfoDispatcher {
 public:
  static TcpInfoDispatcher* getInstance();

  /**
   * Initializes and returns TcpInfo struct.
   *
   * @param fd          Socket file descriptor encapsulated in NetworkSocket.
   * @param options     Options for lookup.
   * @param netopsDispatcher  Dispatcher to use for netops calls;
   *                          facilitates mocking during unit tests.
   * @param ioctlDispatcher   Dispatcher to use for ioctl calls;
   *                          facilitates mocking during unit tests.
   */
  virtual Expected<TcpInfo, std::errc> initFromFd(
      const NetworkSocket& fd,
      const TcpInfo::LookupOptions& options = TcpInfo::LookupOptions(),
      netops::Dispatcher& netopsDispatcher =
          *netops::Dispatcher::getDefaultInstance(),
      TcpInfo::IoctlDispatcher& ioctlDispatcher =
          *TcpInfo::IoctlDispatcher::getDefaultInstance());

 protected:
  TcpInfoDispatcher() = default;
  virtual ~TcpInfoDispatcher() = default;
};

} // namespace folly
