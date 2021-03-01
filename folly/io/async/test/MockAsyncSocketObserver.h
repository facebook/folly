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

#pragma once

#include <folly/io/async/AsyncSocket.h>
#include <folly/portability/GMock.h>

namespace folly {
namespace test {

/*
 * Mock class for AsyncSocketLifecycleObserver.
 *
 * Deriving from MockAsyncTransportLifecycleObserver results in diamond
 * inheritance that creates a mess for Stict/Weak mocks; easier to just derive
 * directly from AsyncSocket::LifecycleObserver and clone mocks
 */
class MockAsyncSocketLifecycleObserver : public AsyncSocket::LifecycleObserver {
 public:
  using AsyncSocket::LifecycleObserver::LifecycleObserver;
  GMOCK_METHOD1_(, noexcept, , observerAttach, void(AsyncTransport*));
  GMOCK_METHOD1_(, noexcept, , observerDetach, void(AsyncTransport*));
  GMOCK_METHOD1_(, noexcept, , destroy, void(AsyncTransport*));
  GMOCK_METHOD1_(, noexcept, , close, void(AsyncTransport*));
  GMOCK_METHOD1_(, noexcept, , connect, void(AsyncTransport*));
  GMOCK_METHOD2_(, noexcept, , evbAttach, void(AsyncTransport*, EventBase*));
  GMOCK_METHOD2_(, noexcept, , evbDetach, void(AsyncTransport*, EventBase*));
  GMOCK_METHOD2_(
      ,
      noexcept,
      ,
      byteEvent,
      void(AsyncTransport*, const AsyncTransport::ByteEvent&));
  GMOCK_METHOD1_(, noexcept, , byteEventsEnabled, void(AsyncTransport*));
  GMOCK_METHOD2_(
      ,
      noexcept,
      ,
      byteEventsUnavailable,
      void(AsyncTransport*, const AsyncSocketException&));

  // additional handlers specific to AsyncSocket::LifecycleObserver
  GMOCK_METHOD1_(, noexcept, , fdDetach, void(AsyncSocket*));
  GMOCK_METHOD2_(, noexcept, , move, void(AsyncSocket*, AsyncSocket*));
};

} // namespace test
} // namespace folly
