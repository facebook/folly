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

#include <folly/io/async/AsyncSignalHandler.h>
#include <folly/io/async/EventBase.h>
#include <folly/io/async/test/Util.h>
#include <folly/portability/GTest.h>

#define FOLLY_SKIP_IF_NULLPTR_BACKEND(evb)                           \
  std::unique_ptr<EventBase> evb##Ptr;                               \
  try {                                                              \
    auto factory = [] {                                              \
      auto backend = TypeParam::getBackend();                        \
      if (!backend) {                                                \
        throw std::runtime_error("backend not available");           \
      }                                                              \
      return backend;                                                \
    };                                                               \
    evb##Ptr = std::make_unique<EventBase>(                          \
        EventBase::Options().setBackendFactory(std::move(factory))); \
  } catch (const std::runtime_error& e) {                            \
    if (std::string("backend not available") == e.what()) {          \
      SKIP() << "Backend not available";                             \
    }                                                                \
  }                                                                  \
  EventBase& evb = *evb##Ptr.get()

namespace folly {
namespace test {
class TestSignalHandler : public AsyncSignalHandler {
 public:
  using AsyncSignalHandler::AsyncSignalHandler;

  void signalReceived(int /* signum */) noexcept override { called = true; }

  bool called{false};
};

template <typename T>
class AsyncSignalHandlerTest : public ::testing::Test {
 public:
  AsyncSignalHandlerTest() = default;
};

TYPED_TEST_SUITE_P(AsyncSignalHandlerTest);

TYPED_TEST_P(AsyncSignalHandlerTest, basic) {
  FOLLY_SKIP_IF_NULLPTR_BACKEND(evb);
  TestSignalHandler handler{&evb};

  handler.registerSignalHandler(SIGUSR1);
  kill(getpid(), SIGUSR1);

  EXPECT_FALSE(handler.called);
  evb.loopOnce(EVLOOP_NONBLOCK);
  EXPECT_TRUE(handler.called);
}

TYPED_TEST_P(AsyncSignalHandlerTest, attachEventBase) {
  TestSignalHandler handler{nullptr};
  EXPECT_FALSE(handler.getEventBase());
  FOLLY_SKIP_IF_NULLPTR_BACKEND(evb);

  handler.attachEventBase(&evb);
  EXPECT_EQ(&evb, handler.getEventBase());

  handler.registerSignalHandler(SIGUSR1);
  kill(getpid(), SIGUSR1);
  EXPECT_FALSE(handler.called);
  evb.loopOnce(EVLOOP_NONBLOCK);
  EXPECT_TRUE(handler.called);

  handler.unregisterSignalHandler(SIGUSR1);
  handler.detachEventBase();
  EXPECT_FALSE(handler.getEventBase());
}

REGISTER_TYPED_TEST_SUITE_P(AsyncSignalHandlerTest, basic, attachEventBase);
} // namespace test
} // namespace folly
