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

#include <map>

#include <folly/net/NetworkSocket.h>
#include <folly/portability/Sockets.h>

namespace folly {

/**
 * Uniquely identifies a handle to a socket option value. Each
 * combination of level and option name corresponds to one socket
 * option value.
 */
class SocketOptionKey {
 public:
  enum class ApplyPos { POST_BIND = 0, PRE_BIND = 1 };

  friend bool operator<(
      const SocketOptionKey& lhs, const SocketOptionKey& rhs) {
    if (lhs.level == rhs.level) {
      return lhs.optname < rhs.optname;
    }
    return lhs.level < rhs.level;
  }

  friend bool operator==(
      const SocketOptionKey& lhs, const SocketOptionKey& rhs) {
    return lhs.level == rhs.level && lhs.optname == rhs.optname;
  }

  int apply(NetworkSocket fd, int val) const;
  int apply(NetworkSocket fd, const void* val, socklen_t len) const;

  int level;
  int optname;
  ApplyPos applyPos_{ApplyPos::POST_BIND};
};

// Maps from a socket option key to its value
using SocketOptionMap = std::map<SocketOptionKey, int>;
using SocketNontrivialOptionMap = std::map<SocketOptionKey, std::string>;

extern const SocketOptionMap emptySocketOptionMap;
extern const SocketNontrivialOptionMap emptySocketNontrivialOptionMap;

using SocketCmsgMap = std::map<SocketOptionKey, int>;
using SocketNontrivialCmsgMap = std::map<SocketOptionKey, std::string>;

int applySocketOptions(
    NetworkSocket fd,
    const SocketOptionMap& options,
    SocketOptionKey::ApplyPos pos);

int applySocketOptions(
    NetworkSocket fd,
    const SocketNontrivialOptionMap& options,
    SocketOptionKey::ApplyPos pos);

SocketOptionMap validateSocketOptions(
    const SocketOptionMap& options,
    sa_family_t family,
    SocketOptionKey::ApplyPos pos);

SocketNontrivialOptionMap validateSocketOptions(
    const SocketNontrivialOptionMap& options,
    sa_family_t family,
    SocketOptionKey::ApplyPos pos);

} // namespace folly
