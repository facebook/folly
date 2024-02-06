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

#include <folly/experimental/io/EpollBackend.h>

#if FOLLY_HAS_EPOLL

#include <folly/io/async/test/AsyncSignalHandlerTestLib.h>
#include <folly/io/async/test/EventBaseTestLib.h>

namespace folly {
namespace test {

struct EpollBackendProvider : BackendProviderBase {
  static std::unique_ptr<folly::EventBaseBackendBase> getBackend() {
    folly::EpollBackend::Options options;
    return std::make_unique<folly::EpollBackend>(options);
  }
};
INSTANTIATE_TYPED_TEST_SUITE_P(
    EventBaseTest, EventBaseTest, EpollBackendProvider);
INSTANTIATE_TYPED_TEST_SUITE_P(
    EventBaseTest1, EventBaseTest1, EpollBackendProvider);
INSTANTIATE_TYPED_TEST_SUITE_P(
    AsyncSignalHandlerTest, AsyncSignalHandlerTest, EpollBackendProvider);

} // namespace test
} // namespace folly

#endif
