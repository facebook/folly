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

#include <sys/types.h>

#include <folly/net/NetworkSocket.h>
#include <folly/portability/Sockets.h>

#if !defined(FOLLY_ALLOW_TFO)
#if defined(__linux__) || defined(__APPLE__)
// only allow for linux right now
#define FOLLY_ALLOW_TFO 1
#endif
#endif

namespace folly {
namespace detail {

/**
 * TFOAvailability describes support for TCP Fast Open (TFO).
 */
enum class TFOAvailability {
  /**
   * TFOAvailability::None indicates that the current environment
   * does not support TFO (or we could not detect support via probing).
   */
  None,
  /**
   * TFOAvailability::WithCookies indicates that TFO is supported by the
   * platform. However, in order for TFO data to be used or for TFO to
   * be attempted, a prior connection to the same peer must have successfully
   * taken place so that the peer could issue a cookie.
   */
  WithCookies,
  /**
   * TFOAvailability::Unconditional indicates that TFO will always be
   * unconditionally sent or accepted, regardless of whether or not prior
   * connections have taken place.
   */
  Unconditional
};

/**
 * PlatformTFOSettings describes TFO support for client connections and
 * server connections.
 */
struct PlatformTFOSettings {
  TFOAvailability client{TFOAvailability::None};
  TFOAvailability server{TFOAvailability::None};
};

/**
 * tfo_sendto has the same semantics as sendmsg, but is used to
 * send with TFO data.
 */
ssize_t tfo_sendmsg(NetworkSocket sockfd, const struct msghdr* msg, int flags);

/**
 * Enable TFO on a listening socket.
 */
int tfo_enable(NetworkSocket sockfd, size_t max_queue_size);

/**
 * Check if TFO succeeded in being used.
 */
bool tfo_succeeded(NetworkSocket sockfd);

/**
 * Check if TFO is supported by the kernel.
 */
PlatformTFOSettings tfo_platform_availability();

} // namespace detail
} // namespace folly
