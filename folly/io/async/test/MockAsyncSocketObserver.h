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
 * directly from AsyncSocket::LegacyLifecycleObserver and clone mocks
 */
class MockAsyncSocketLifecycleObserver
    : public AsyncSocket::LegacyLifecycleObserver {
 public:
  using AsyncSocket::LegacyLifecycleObserver::LegacyLifecycleObserver;
  MOCK_METHOD(void, observerAttachMock, (AsyncTransport*));
  MOCK_METHOD(void, observerDetachMock, (AsyncTransport*));
  MOCK_METHOD(void, destroyMock, (AsyncTransport*));
  MOCK_METHOD(void, closeMock, (AsyncSocket*));
  MOCK_METHOD(void, connectAttemptMock, (AsyncSocket*));
  MOCK_METHOD(void, connectSuccessMock, (AsyncSocket*));
  MOCK_METHOD(
      void, connectErrorMock, (AsyncSocket*, const AsyncSocketException&));
  MOCK_METHOD(void, evbAttachMock, (AsyncSocket*, EventBase*));
  MOCK_METHOD(void, evbDetachMock, (AsyncSocket*, EventBase*));
  MOCK_METHOD(
      void, byteEventMock, (AsyncSocket*, const AsyncTransport::ByteEvent&));
  MOCK_METHOD(void, byteEventsEnabledMock, (AsyncSocket*));
  MOCK_METHOD(
      void,
      byteEventsUnavailableMock,
      (AsyncSocket*, const AsyncSocketException&));

  // additional handlers specific to AsyncSocket::LegacyLifecycleObserver
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
  void close(AsyncSocket* socket) noexcept override { closeMock(socket); }
  void connectAttempt(AsyncSocket* socket) noexcept override {
    connectAttemptMock(socket);
  }
  void connectSuccess(AsyncSocket* socket) noexcept override {
    connectSuccessMock(socket);
  }
  void connectError(
      AsyncSocket* socket, const AsyncSocketException& ex) noexcept override {
    connectErrorMock(socket, ex);
  }
  void evbAttach(AsyncSocket* socket, EventBase* eb) noexcept override {
    evbAttachMock(socket, eb);
  }
  void evbDetach(AsyncSocket* socket, EventBase* eb) noexcept override {
    evbDetachMock(socket, eb);
  }
  void byteEvent(
      AsyncSocket* socket,
      const AsyncTransport::ByteEvent& ev) noexcept override {
    byteEventMock(socket, ev);
  }
  void byteEventsEnabled(AsyncSocket* socket) noexcept override {
    byteEventsEnabledMock(socket);
  }
  void byteEventsUnavailable(
      AsyncSocket* trans, const AsyncSocketException& ex) noexcept override {
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
