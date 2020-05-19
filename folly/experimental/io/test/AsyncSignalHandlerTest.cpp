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

#include <folly/experimental/io/IoUringBackend.h>
#include <folly/io/async/test/AsyncSignalHandlerTestLib.h>

namespace folly {
namespace test {
static constexpr size_t kCapacity = 16 * 1024;
static constexpr size_t kMaxSubmit = 128;
static constexpr size_t kMaxGet = static_cast<size_t>(-1);

struct IoUringBackendProvider {
  static std::unique_ptr<folly::EventBaseBackendBase> getBackend() {
    try {
      folly::PollIoBackend::Options options;
      options.setCapacity(kCapacity)
          .setMaxSubmit(kMaxSubmit)
          .setMaxGet(kMaxGet)
          .setUseRegisteredFds(false);

      return std::make_unique<folly::IoUringBackend>(options);
    } catch (const IoUringBackend::NotAvailable&) {
      return nullptr;
    }
  }
};

INSTANTIATE_TYPED_TEST_CASE_P(
    AsyncSignalHandlerTest,
    AsyncSignalHandlerTest,
    IoUringBackendProvider);
} // namespace test
} // namespace folly
