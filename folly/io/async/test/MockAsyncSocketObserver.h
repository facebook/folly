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
  MOCK_METHOD1(observerAttachMock, void(AsyncTransport*));
  MOCK_METHOD1(observerDetachMock, void(AsyncTransport*));
  MOCK_METHOD1(destroyMock, void(AsyncTransport*));
  MOCK_METHOD1(closeMock, void(AsyncTransport*));
  MOCK_METHOD1(connectMock, void(AsyncTransport*));
  MOCK_METHOD2(evbAttachMock, void(AsyncTransport*, EventBase*));
  MOCK_METHOD2(evbDetachMock, void(AsyncTransport*, EventBase*));
  MOCK_METHOD2(
      byteEventMock, void(AsyncTransport*, const AsyncTransport::ByteEvent&));
  MOCK_METHOD1(byteEventsEnabledMock, void(AsyncTransport*));
  MOCK_METHOD2(
      byteEventsUnavailableMock,
      void(AsyncTransport*, const AsyncSocketException&));

  // additional handlers specific to AsyncSocket::LifecycleObserver
  MOCK_METHOD1(fdDetachMock, void(AsyncSocket*));
  MOCK_METHOD2(moveMock, void(AsyncSocket*, AsyncSocket*));

 private:
  void observerAttach(AsyncTransport* trans) noexcept override {
    observerAttachMock(trans);
  }
  void observerDetach(AsyncTransport* trans) noexcept override {
    observerDetachMock(trans);
  }
  void destroy(AsyncTransport* trans) noexcept override { destroyMock(trans); }
  void close(AsyncTransport* trans) noexcept override { closeMock(trans); }
  void connect(AsyncTransport* trans) noexcept override { connectMock(trans); }
  void evbAttach(AsyncTransport* trans, EventBase* eb) noexcept override {
    evbAttachMock(trans, eb);
  }
  void evbDetach(AsyncTransport* trans, EventBase* eb) noexcept override {
    evbDetachMock(trans, eb);
  }
  void byteEvent(
      AsyncTransport* trans,
      const AsyncTransport::ByteEvent& ev) noexcept override {
    byteEventMock(trans, ev);
  }
  void byteEventsEnabled(AsyncTransport* trans) noexcept override {
    byteEventsEnabledMock(trans);
  }
  void byteEventsUnavailable(
      AsyncTransport* trans, const AsyncSocketException& ex) noexcept override {
    byteEventsUnavailableMock(trans, ex);
  }
  void fdDetach(AsyncSocket* sock) noexcept override { fdDetachMock(sock); }
  void move(AsyncSocket* olds, AsyncSocket* news) noexcept override {
    moveMock(olds, news);
  }
};

} // namespace test
} // namespace folly
