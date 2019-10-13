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

#include <folly/test/BufferedAtomic.h>

namespace folly {
namespace test {

// access is protected by futexLock
static std::unordered_map<
    const detail::Futex<BufferedDeterministicAtomic>*,
    std::list<std::pair<uint32_t, bool*>>>
    futexQueues;

static std::mutex futexLock;

int futexWakeImpl(
    const folly::detail::Futex<test::BufferedDeterministicAtomic>* futex,
    int count,
    uint32_t wakeMask) {
  return deterministicFutexWakeImpl<test::BufferedDeterministicAtomic>(
      futex, futexLock, futexQueues, count, wakeMask);
}

folly::detail::FutexResult futexWaitImpl(
    const folly::detail::Futex<test::BufferedDeterministicAtomic>* futex,
    uint32_t expected,
    std::chrono::system_clock::time_point const* absSystemTime,
    std::chrono::steady_clock::time_point const* absSteadyTime,
    uint32_t waitMask) {
  return deterministicFutexWaitImpl<test::BufferedDeterministicAtomic>(
      futex,
      futexLock,
      futexQueues,
      expected,
      absSystemTime,
      absSteadyTime,
      waitMask);
}

} // namespace test
} // namespace folly
