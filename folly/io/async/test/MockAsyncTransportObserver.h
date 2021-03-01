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

#include <folly/io/async/AsyncSocketException.h>
#include <folly/io/async/AsyncTransport.h>
#include <folly/portability/GMock.h>

namespace folly {
namespace test {

class MockAsyncTransportLifecycleObserver
    : public AsyncTransport::LifecycleObserver {
 public:
  using AsyncTransport::LifecycleObserver::LifecycleObserver;
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
    ON_CALL(*this, byteEvent(testing::_, testing::_))
        .WillByDefault(
            testing::Invoke([this](
                                AsyncTransport* transport,
                                const AsyncTransport::ByteEvent& event) {
              CHECK_EQ(this->transport_, transport);
              byteEvents_.emplace_back(event);
            }));
    ON_CALL(*this, byteEventsEnabled(testing::_))
        .WillByDefault(testing::Invoke([this](AsyncTransport* transport) {
          CHECK_EQ(this->transport_, transport);
          byteEventsEnabledCalled_++;
        }));

    ON_CALL(*this, byteEventsUnavailable(testing::_, testing::_))
        .WillByDefault(testing::Invoke(
            [this](AsyncTransport* transport, const AsyncSocketException& ex) {
              CHECK_EQ(this->transport_, transport);
              byteEventsUnavailableCalled_++;
              byteEventsUnavailableCalledEx_.emplace(ex);
            }));
    transport->addLifecycleObserver(this);
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
