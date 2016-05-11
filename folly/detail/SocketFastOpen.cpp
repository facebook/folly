/*
 * Copyright 2016 Facebook, Inc.
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

#include <folly/detail/SocketFastOpen.h>

namespace folly {
namespace detail {

#if FOLLY_ALLOW_TFO
ssize_t tfo_sendto(
    int sockfd,
    const void* buf,
    size_t len,
    int flags,
    const struct sockaddr* dest_addr,
    socklen_t addrlen) {
  flags |= MSG_FASTOPEN;
  return sendto(sockfd, buf, len, flags, dest_addr, addrlen);
}

int tfo_enable(int sockfd, size_t max_queue_size) {
  return setsockopt(
      sockfd, SOL_TCP, TCP_FASTOPEN, &max_queue_size, sizeof(max_queue_size));
}
#endif
}
}
