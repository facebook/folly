/*
 *  Copyright (c) 2015, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include <folly/wangle/acceptor/SocketOptions.h>

#include <netinet/tcp.h>
#include <sys/socket.h>

namespace folly {

AsyncSocket::OptionMap filterIPSocketOptions(
  const AsyncSocket::OptionMap& allOptions,
  const int addrFamily) {
  AsyncSocket::OptionMap opts;
  int exclude;
  if (addrFamily == AF_INET) {
    exclude = IPPROTO_IPV6;
  } else if (addrFamily == AF_INET6) {
    exclude = IPPROTO_IP;
  } else {
    LOG(FATAL) << "Address family " << addrFamily << " was not IPv4 or IPv6";
    return opts;
  }
  for (const auto& opt: allOptions) {
    if (opt.first.level != exclude) {
      opts[opt.first] = opt.second;
    }
  }
  return opts;
}

}
