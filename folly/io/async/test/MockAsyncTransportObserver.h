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

#include <folly/io/async/AsyncSocketException.h>
#include <folly/io/async/AsyncTransport.h>
#include <folly/portability/GMock.h>

namespace folly {
namespace test {

class MockAsyncTransportLifecycleObserver
    : public AsyncTransport::LifecycleObserver {
 public:
  using AsyncTransport::LifecycleObserver::LifecycleObserver;
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
  MOCK_METHOD(
      PrewriteRequest, prewriteMock, (AsyncTransport*, const PrewriteState&));

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
  PrewriteRequest prewrite(
      AsyncTransport* trans, const PrewriteState& state) noexcept override {
    return prewriteMock(trans, state);
  }
};

/**
 * Extends mock class to simplify ByteEvents tests.
 */
class MockAsyncTransportObserverForByteEvents
    : public MockAsyncTransportLifecycleObserver {
 public:
  MockAsyncTransportObserverForByteEvents(
      AsyncTransport* transport,
      const MockAsyncTransportObserverForByteEvents::Config& observerConfig)
      : MockAsyncTransportLifecycleObserver(observerConfig),
        transport_(transport) {
    ON_CALL(*this, byteEventMock(testing::_, testing::_))
        .WillByDefault(
            testing::Invoke([this](
                                AsyncTransport* transport,
                                const AsyncTransport::ByteEvent& event) {
              CHECK_EQ(this->transport_, transport);
              byteEvents_.emplace_back(event);
            }));
    ON_CALL(*this, byteEventsEnabledMock(testing::_))
        .WillByDefault(testing::Invoke([this](AsyncTransport* transport) {
          CHECK_EQ(this->transport_, transport);
          byteEventsEnabledCalled_++;
        }));

    ON_CALL(*this, byteEventsUnavailableMock(testing::_, testing::_))
        .WillByDefault(testing::Invoke(
            [this](AsyncTransport* transport, const AsyncSocketException& ex) {
              CHECK_EQ(this->transport_, transport);
              byteEventsUnavailableCalled_++;
              byteEventsUnavailableCalledEx_.emplace(ex);
            }));
    transport->addLifecycleObserver(this);
  }

  const std::vector<AsyncTransport::ByteEvent>& getByteEvents() {
    return byteEvents_;
  }

  folly::Optional<AsyncTransport::ByteEvent> getByteEventReceivedWithOffset(
      const uint64_t offset, const AsyncTransport::ByteEvent::Type type) {
    for (const auto& byteEvent : byteEvents_) {
      if (type == byteEvent.type && offset == byteEvent.offset) {
        return byteEvent;
      }
    }
    return folly::none;
  }

  folly::Optional<uint64_t> maxOffsetForByteEventReceived(
      const AsyncTransport::ByteEvent::Type type) {
    folly::Optional<uint64_t> maybeMaxOffset;
    for (const auto& byteEvent : byteEvents_) {
      if (type == byteEvent.type &&
          (!maybeMaxOffset.has_value() ||
           maybeMaxOffset.value() <= byteEvent.offset)) {
        maybeMaxOffset = byteEvent.offset;
      }
    }
    return maybeMaxOffset;
  }

  bool checkIfByteEventReceived(
      const AsyncTransport::ByteEvent::Type type, const uint64_t offset) {
    for (const auto& byteEvent : byteEvents_) {
      if (type == byteEvent.type && offset == byteEvent.offset) {
        return true;
      }
    }
    return false;
  }

  void waitForByteEvent(
      const AsyncTransport::ByteEvent::Type type, const uint64_t offset) {
    while (!checkIfByteEventReceived(type, offset)) {
      transport_->getEventBase()->loopOnce();
    }
  }

  // Exposed ByteEvent helper fields with const
  const uint32_t& byteEventsEnabledCalled{byteEventsEnabledCalled_};
  const uint32_t& byteEventsUnavailableCalled{byteEventsUnavailableCalled_};
  const folly::Optional<AsyncSocketException>& byteEventsUnavailableCalledEx{
      byteEventsUnavailableCalledEx_};
  const std::vector<AsyncTransport::ByteEvent>& byteEvents{byteEvents_};

 private:
  const AsyncTransport* transport_;

  // ByteEvents helpers
  uint32_t byteEventsEnabledCalled_{0};
  uint32_t byteEventsUnavailableCalled_{0};
  folly::Optional<AsyncSocketException> byteEventsUnavailableCalledEx_;
  std::vector<AsyncTransport::ByteEvent> byteEvents_;
};

} // namespace test
} // namespace folly
