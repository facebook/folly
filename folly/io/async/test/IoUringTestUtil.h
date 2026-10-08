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

#include <memory>

#include <folly/io/async/EventBase.h>
#include <folly/io/async/IoUringBackend.h>

#if FOLLY_HAS_LIBURING

namespace folly::test {

// io_uring rings are charged to RLIMIT_MEMLOCK, a budget shared by all
// processes of the same user on the host, so keep them small. The charge scales
// with the ring capacity and the provided-buffer count, not the buffer size.
IoUringBackend::Options ioUringOptions();
IoUringBackend::Options ioUringOptionsWithProvidedBuffers();

// Under CI load, other processes can transiently exhaust the RLIMIT_MEMLOCK
// budget, so retry until they release their rings.
std::unique_ptr<EventBase> makeIoUringEventBase(
    IoUringBackend::Options (*makeOptions)());

} // namespace folly::test

#endif
