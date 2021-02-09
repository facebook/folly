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

#include <folly/io/async/test/AsyncSignalHandlerTestLib.h>

namespace folly {
namespace test {
struct DefaultBackendProvider {
  static std::unique_ptr<folly::EventBaseBackendBase> getBackend() {
    return folly::EventBase::getDefaultBackend();
  }
};

INSTANTIATE_TYPED_TEST_CASE_P(
    AsyncSignalHandlerTest, AsyncSignalHandlerTest, DefaultBackendProvider);

TEST(AsyncSignalHandler, destructionOrder) {
  auto evb = std::make_unique<EventBase>();
  TestSignalHandler handler(evb.get());
  handler.registerSignalHandler(SIGHUP);

  // Test a situation where the EventBase is destroyed with an
  // AsyncSignalHandler still installed.  Destroying the AsyncSignalHandler
  // after the EventBase should work normally and should not crash or access
  // invalid memory.
  evb.reset();
}

} // namespace test
} // namespace folly
