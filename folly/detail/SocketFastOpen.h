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

#pragma once

#include <folly/portability/Sockets.h>
#include <sys/types.h>

#if !defined(FOLLY_ALLOW_TFO) && defined(TCP_FASTOPEN) && defined(MSG_FASTOPEN)
#define FOLLY_ALLOW_TFO 1
#endif

namespace folly {
namespace detail {

#if FOLLY_ALLOW_TFO

/**
 * tfo_sendto has the same semantics as sendto, but is used to
 * send with TFO data.
 */
ssize_t tfo_sendto(
    int sockfd,
    const void* buf,
    size_t len,
    int flags,
    const struct sockaddr* dest_addr,
    socklen_t addlen);

/**
 * Enable TFO on a listening socket.
 */
int tfo_enable(int sockfd, size_t max_queue_size);
#endif
}
}
