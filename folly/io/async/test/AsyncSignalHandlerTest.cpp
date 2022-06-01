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

#include <csignal>
#include <folly/io/async/test/AsyncSignalHandlerTestLib.h>
#include <folly/portability/Unistd.h>

namespace folly {
namespace test {
struct DefaultBackendProvider {
  static std::unique_ptr<folly::EventBaseBackendBase> getBackend() {
    return folly::EventBase::getDefaultBackend();
  }
};

INSTANTIATE_TYPED_TEST_SUITE_P(
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

TEST(CallbackAsyncSignalHandler, callback) {
  EventBase evb;
  int sig_recv = 0;
  const int sig_send = SIGUSR1;

  CallbackAsyncSignalHandler handler{
      &evb, [&sig_recv](int signum) { sig_recv = signum; }};
  handler.registerSignalHandler(sig_send);

  // send the signal to this pid, effectively queuing the callback
  const int error = raise(sig_send);
  ASSERT_EQ(0, error);

  // callback should have not yet fired
  ASSERT_EQ(0, sig_recv);

  // dispatch the callback waiting in the queue
  evb.loopOnce(EVLOOP_NONBLOCK);

  // callback should have fired
  ASSERT_EQ(sig_send, sig_recv);
}

} // namespace test
} // namespace folly
