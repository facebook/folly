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

#include <folly/net/TcpInfoDispatcher.h>

namespace folly {

TcpInfoDispatcher* TcpInfoDispatcher::getInstance() {
  static TcpInfoDispatcher dispatcher = {};
  return &dispatcher;
}

Expected<TcpInfo, std::errc> TcpInfoDispatcher::initFromFd(
    const NetworkSocket& fd,
    const TcpInfo::LookupOptions& options,
    netops::Dispatcher& netopsDispatcher,
    TcpInfo::IoctlDispatcher& ioctlDispatcher) {
  return TcpInfo::initFromFd(fd, options, netopsDispatcher, ioctlDispatcher);
}

} // namespace folly
