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

#include <folly/io/async/test/IoUringTestUtil.h>

#include <algorithm>
#include <chrono>
#include <thread>

#if FOLLY_HAS_LIBURING

namespace folly::test {

IoUringBackend::Options ioUringOptions() {
  IoUringBackend::Options options;
  options.setCapacity(64).setMaxSubmit(32);
  return options;
}

IoUringBackend::Options ioUringOptionsWithProvidedBuffers() {
  auto options = ioUringOptions();
  options.setInitialProvidedBuffers(2048, 256);
  return options;
}

std::unique_ptr<EventBase> makeIoUringEventBase(
    IoUringBackend::Options (*makeOptions)()) {
  constexpr auto kMaxBackoff = std::chrono::milliseconds(1000);
  const auto deadline =
      std::chrono::steady_clock::now() + std::chrono::seconds(60);
  auto backoff = std::chrono::milliseconds(10);
  while (true) {
    try {
      return std::make_unique<EventBase>(EventBase::Options{}.setBackendFactory(
          [makeOptions]() -> std::unique_ptr<EventBaseBackendBase> {
            return std::make_unique<IoUringBackend>(makeOptions());
          }));
    } catch (IoUringBackend::OutOfMemory const&) {
      if (std::chrono::steady_clock::now() + backoff > deadline) {
        throw;
      }
    }
    // NOLINTNEXTLINE(facebook-hte-BadCall-sleep_for)
    std::this_thread::sleep_for(backoff);
    backoff = std::min(backoff * 2, kMaxBackoff);
  }
}

} // namespace folly::test

#endif
