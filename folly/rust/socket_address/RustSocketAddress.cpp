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

#include <memory>
#include <folly/rust/socket_address/RustSocketAddress.h>

namespace facebook::rust {

std::unique_ptr<folly::SocketAddress> socketaddress_create(
    const std::string& host, uint16_t port, bool allowNameLookup) {
  return std::make_unique<folly::SocketAddress>(host, port, allowNameLookup);
}

std::unique_ptr<folly::SocketAddress>
socketaddress_create_from_ipaddress_and_port(
    const folly::IPAddress& ipAddress, uint16_t port) {
  return std::make_unique<folly::SocketAddress>(ipAddress, port);
}

std::unique_ptr<folly::SocketAddress> socketaddress_from_path(
    const std::string& path) {
  return std::make_unique<folly::SocketAddress>(
      folly::SocketAddress::makeFromPath(path));
}

std::unique_ptr<std::string> socketaddress_describe(
    const folly::SocketAddress& addr) {
  return std::make_unique<std::string>(addr.describe());
}

std::unique_ptr<folly::IPAddress> socketaddress_get_ip_address(
    const folly::SocketAddress& addr) {
  return std::make_unique<folly::IPAddress>(addr.getIPAddress());
}

uint16_t socketaddress_get_port(const folly::SocketAddress& addr) {
  return addr.getPort();
}

bool socketaddress_is_initialized(const folly::SocketAddress& addr) {
  return addr.isInitialized();
}

} // namespace facebook::rust
