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

#include <folly/io/async/AsyncSocket.h>
#include <folly/io/async/AsyncSocketException.h>
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
  MOCK_METHOD(void, observerAttachMock, (AsyncTransport*));
  MOCK_METHOD(void, observerDetachMock, (AsyncTransport*));
  MOCK_METHOD(void, destroyMock, (AsyncTransport*));
  MOCK_METHOD(void, closeMock, (AsyncTransport*));
  MOCK_METHOD(void, connectAttemptMock, (AsyncTransport*));
  MOCK_METHOD(void, connectSuccessMock, (AsyncTransport*));
  MOCK_METHOD(
      void, connectErrorMock, (AsyncTransport*, const AsyncSocketException&));
  MOCK_METHOD(void, evbAttachMock, (AsyncTransport*, EventBase*));
  MOCK_METHOD(void, evbDetachMock, (AsyncTransport*, EventBase*));
  MOCK_METHOD(
      void, byteEventMock, (AsyncTransport*, const AsyncTransport::ByteEvent&));
  MOCK_METHOD(void, byteEventsEnabledMock, (AsyncTransport*));
  MOCK_METHOD(
      void,
      byteEventsUnavailableMock,
      (AsyncTransport*, const AsyncSocketException&));

  // additional handlers specific to AsyncSocket::LifecycleObserver
  MOCK_METHOD(void, fdDetachMock, (AsyncSocket*));
  MOCK_METHOD(void, fdAttachMock, (AsyncSocket*));
  MOCK_METHOD(void, moveMock, (AsyncSocket*, AsyncSocket*));

 private:
  void observerAttach(AsyncTransport* trans) noexcept override {
    observerAttachMock(trans);
  }
  void observerDetach(AsyncTransport* trans) noexcept override {
    observerDetachMock(trans);
  }
  void destroy(AsyncTransport* trans) noexcept override { destroyMock(trans); }
  void close(AsyncTransport* trans) noexcept override { closeMock(trans); }
  void connectAttempt(AsyncTransport* trans) noexcept override {
    connectAttemptMock(trans);
  }
  void connectSuccess(AsyncTransport* trans) noexcept override {
    connectSuccessMock(trans);
  }
  void connectError(
      AsyncTransport* trans, const AsyncSocketException& ex) noexcept override {
    connectErrorMock(trans, ex);
  }
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
  void fdAttach(AsyncSocket* sock) noexcept override { fdAttachMock(sock); }
  void move(AsyncSocket* olds, AsyncSocket* news) noexcept override {
    moveMock(olds, news);
  }
};

} // namespace test
} // namespace folly
