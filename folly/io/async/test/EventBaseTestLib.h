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

#include <atomic>
#include <iostream>
#include <memory>
#include <thread>

#include <folly/Memory.h>
#include <folly/ScopeGuard.h>
#include <folly/futures/Promise.h>
#include <folly/io/async/AsyncTimeout.h>
#include <folly/io/async/EventBase.h>
#include <folly/io/async/EventHandler.h>
#include <folly/io/async/test/SocketPair.h>
#include <folly/io/async/test/Util.h>
#include <folly/portability/Stdlib.h>
#include <folly/portability/Unistd.h>
#include <folly/synchronization/Baton.h>
#include <folly/system/ThreadName.h>

#define FOLLY_SKIP_IF_NULLPTR_BACKEND_WITH_OPTS(evb, opts)  \
  std::unique_ptr<EventBase> evb##Ptr;                      \
  try {                                                     \
    auto factory = [] {                                     \
      auto backend = TypeParam::getBackend();               \
      if (!backend) {                                       \
        throw std::runtime_error("backend not available");  \
      }                                                     \
      return backend;                                       \
    };                                                      \
    auto evbOpts = opts;                                    \
    evb##Ptr = std::make_unique<EventBase>(                 \
        opts.setBackendFactory(std::move(factory)));        \
  } catch (const std::runtime_error& e) {                   \
    if (std::string("backend not available") == e.what()) { \
      SKIP() << "Backend not available";                    \
    }                                                       \
  }                                                         \
  EventBase& evb = *evb##Ptr.get()

#define FOLLY_SKIP_IF_NULLPTR_BACKEND(evb) \
  FOLLY_SKIP_IF_NULLPTR_BACKEND_WITH_OPTS(evb, EventBase::Options())

///////////////////////////////////////////////////////////////////////////
// Tests for read and write events
///////////////////////////////////////////////////////////////////////////

namespace folly {
namespace test {
class EventBaseTestBase : public ::testing::Test {
 public:
  EventBaseTestBase() {
    // libevent 2.x uses a coarse monotonic timer by default on Linux.
    // This timer is imprecise enough to cause several of our tests to fail.
    //
    // Set an environment variable that causes libevent to use a non-coarse
    // timer. This can be controlled programmatically by using the
    // EVENT_BASE_FLAG_PRECISE_TIMER flag with event_base_new_with_config().
    // However, this would require more compile-time #ifdefs to tell if we are
    // using libevent 2.1+ or not.  Simply using the environment variable is
    // the easiest option for now.
    setenv("EVENT_PRECISE_TIMER", "1", 1);
  }
};

template <typename T>
class EventBaseTest : public EventBaseTestBase {
 public:
  EventBaseTest() = default;
};

TYPED_TEST_SUITE_P(EventBaseTest);

template <typename T>
class EventBaseTest1 : public EventBaseTestBase {
 public:
  EventBaseTest1() = default;
};

template <class Factory>
std::unique_ptr<EventBase> getEventBase(
    folly::EventBase::Options opts = folly::EventBase::Options()) {
  try {
    auto factory = [] {
      auto backend = Factory::getBackend();
      if (!backend) {
        throw std::runtime_error("backend not available");
      }
      return backend;
    };
    return std::make_unique<EventBase>(
        opts.setBackendFactory(std::move(factory)));
  } catch (const std::runtime_error& e) {
    if (std::string("backend not available") == e.what()) {
      return nullptr;
    }
    throw;
  }
}

TYPED_TEST_SUITE_P(EventBaseTest1);

enum { BUF_SIZE = 4096 };

FOLLY_ALWAYS_INLINE ssize_t writeToFD(int fd, size_t length) {
  // write an arbitrary amount of data to the fd
  auto bufv = std::vector<char>(length);
  auto buf = bufv.data();
  memset(buf, 'a', length);
  ssize_t rc = write(fd, buf, length);
  CHECK_EQ(rc, length);
  return rc;
}

FOLLY_ALWAYS_INLINE size_t writeUntilFull(int fd) {
  // Write to the fd until EAGAIN is returned
  size_t bytesWritten = 0;
  char buf[BUF_SIZE];
  memset(buf, 'a', sizeof(buf));
  while (true) {
    ssize_t rc = write(fd, buf, sizeof(buf));
    if (rc < 0) {
      CHECK_EQ(errno, EAGAIN);
      break;
    } else {
      bytesWritten += rc;
    }
  }
  return bytesWritten;
}

FOLLY_ALWAYS_INLINE ssize_t readFromFD(int fd, size_t length) {
  // write an arbitrary amount of data to the fd
  auto buf = std::vector<char>(length);
  return read(fd, buf.data(), length);
}

FOLLY_ALWAYS_INLINE size_t readUntilEmpty(int fd) {
  // Read from the fd until EAGAIN is returned
  char buf[BUF_SIZE];
  size_t bytesRead = 0;
  while (true) {
    int rc = read(fd, buf, sizeof(buf));
    if (rc == 0) {
      CHECK(false) << "unexpected EOF";
    } else if (rc < 0) {
      CHECK_EQ(errno, EAGAIN);
      break;
    } else {
      bytesRead += rc;
    }
  }
  return bytesRead;
}

FOLLY_ALWAYS_INLINE void checkReadUntilEmpty(int fd, size_t expectedLength) {
  ASSERT_EQ(readUntilEmpty(fd), expectedLength);
}

struct ScheduledEvent {
  int milliseconds;
  uint16_t events;
  size_t length;
  ssize_t result;

  void perform(int fd) {
    if (events & folly::EventHandler::READ) {
      if (length == 0) {
        result = readUntilEmpty(fd);
      } else {
        result = readFromFD(fd, length);
      }
    }
    if (events & folly::EventHandler::WRITE) {
      if (length == 0) {
        result = writeUntilFull(fd);
      } else {
        result = writeToFD(fd, length);
      }
    }
  }
};

FOLLY_ALWAYS_INLINE void scheduleEvents(
    EventBase* eventBase, int fd, ScheduledEvent* events) {
  for (ScheduledEvent* ev = events; ev->milliseconds > 0; ++ev) {
    eventBase->tryRunAfterDelay(
        std::bind(&ScheduledEvent::perform, ev, fd), ev->milliseconds);
  }
}

class TestObserver : public folly::ExecutionObserver {
 public:
  virtual void starting(uintptr_t /* id */) noexcept override {
    if (nestedStart_ == 0) {
      nestedStart_ = 1;
    }
    numStartingCalled_++;
  }
  virtual void stopped(uintptr_t /* id */) noexcept override {
    nestedStart_--;
    numStoppedCalled_++;
  }

  int nestedStart_{0};
  int numStartingCalled_{0};
  int numStoppedCalled_{0};
};

class TestEventBaseObserver : public folly::EventBaseObserver {
 public:
  explicit TestEventBaseObserver(uint32_t samplingRatio)
      : samplingRatio_(samplingRatio) {}
  uint32_t getSampleRate() const override { return samplingRatio_; }

  void loopSample(int64_t, int64_t) override { numTimesCalled_++; }
  uint32_t getNumTimesCalled() const { return numTimesCalled_; }

 private:
  uint32_t samplingRatio_;
  uint32_t numTimesCalled_{0};
};

class TestHandler : public folly::EventHandler {
 public:
  TestHandler(folly::EventBase* eventBase, int fd)
      : EventHandler(eventBase, folly::NetworkSocket::fromFd(fd)), fd_(fd) {}

  void handlerReady(uint16_t events) noexcept override {
    ssize_t bytesRead = 0;
    ssize_t bytesWritten = 0;
    if (events & READ) {
      // Read all available data, so EventBase will stop calling us
      // until new data becomes available
      bytesRead = readUntilEmpty(fd_);
    }
    if (events & WRITE) {
      // Write until the pipe buffer is full, so EventBase will stop calling
      // us until the other end has read some data
      bytesWritten = writeUntilFull(fd_);
    }

    log.emplace_back(events, bytesRead, bytesWritten);
  }

  struct EventRecord {
    EventRecord(uint16_t events_, size_t bytesRead_, size_t bytesWritten_)
        : events(events_),
          timestamp(),
          bytesRead(bytesRead_),
          bytesWritten(bytesWritten_) {}

    uint16_t events;
    folly::TimePoint timestamp;
    ssize_t bytesRead;
    ssize_t bytesWritten;
  };

  std::deque<EventRecord> log;

 private:
  int fd_;
};

TYPED_TEST_P(EventBaseTest, EventBaseThread) {
  const auto testInLoop = [](EventBase& evb, bool canRunImmediately) {
    bool done = false;
    evb.runInEventBaseThread([&] {
      evb.checkIsInEventBaseThread();
      EXPECT_TRUE(evb.isInEventBaseThread());
      done = true;
    });
    evb.loopOnce();
    ASSERT_TRUE(done);

    done = false;
    evb.runImmediatelyOrRunInEventBaseThread([&] { done = true; });
    EXPECT_EQ(done, canRunImmediately);
    evb.loopOnce();
    EXPECT_TRUE(done);
  };

  {
    auto evbPtr = getEventBase<TypeParam>();
    EXPECT_TRUE(evbPtr->isInEventBaseThread());
    testInLoop(*evbPtr, true);
    evbPtr->checkIsInEventBaseThread();
  }

  {
    auto evbPtr =
        getEventBase<TypeParam>(EventBase::Options().setStrictLoopThread(true));
    EXPECT_FALSE(evbPtr->isInEventBaseThread());
    testInLoop(*evbPtr, false);
    EXPECT_DEATH(
        evbPtr->checkIsInEventBaseThread(),
        ".*This logic must be executed in the event base thread.*");
  }
}

/**
 * Test a READ event
 */
TYPED_TEST_P(EventBaseTest, ReadEvent) {
  auto evbPtr = getEventBase<TypeParam>();
  SKIP_IF(!evbPtr) << "Backend not available";
  folly::EventBase& eb = *evbPtr;
  SocketPair sp;

  // Register for read events
  TestHandler handler(&eb, sp[0]);
  handler.registerHandler(EventHandler::READ);

  // Register timeouts to perform two write events
  ScheduledEvent events[] = {
      {10, EventHandler::WRITE, 2345, 0},
      {160, EventHandler::WRITE, 99, 0},
      {0, 0, 0, 0},
  };
  TimePoint start;
  scheduleEvents(&eb, sp[1], events);

  // Loop
  eb.loop();
  TimePoint end;

  // Since we didn't use the EventHandler::PERSIST flag, the handler should
  // have received the first read, then unregistered itself.  Check that only
  // the first chunk of data was received.
  ASSERT_EQ(handler.log.size(), 1);
  ASSERT_EQ(handler.log[0].events, EventHandler::READ);
  T_CHECK_TIMEOUT(
      start,
      handler.log[0].timestamp,
      std::chrono::milliseconds(events[0].milliseconds),
      std::chrono::milliseconds(90));
  ASSERT_EQ(handler.log[0].bytesRead, events[0].length);
  ASSERT_EQ(handler.log[0].bytesWritten, 0);
  T_CHECK_TIMEOUT(
      start,
      end,
      std::chrono::milliseconds(events[1].milliseconds),
      std::chrono::milliseconds(30));

  // Make sure the second chunk of data is still waiting to be read.
  size_t bytesRemaining = readUntilEmpty(sp[0]);
  ASSERT_EQ(bytesRemaining, events[1].length);
}

/**
 * Test (READ | PERSIST)
 */
TYPED_TEST_P(EventBaseTest, ReadPersist) {
  auto evbPtr = getEventBase<TypeParam>();
  SKIP_IF(!evbPtr) << "Backend not available";
  folly::EventBase& eb = *evbPtr;
  SocketPair sp;

  // Register for read events
  TestHandler handler(&eb, sp[0]);
  handler.registerHandler(EventHandler::READ | EventHandler::PERSIST);

  // Register several timeouts to perform writes
  ScheduledEvent events[] = {
      {10, EventHandler::WRITE, 1024, 0},
      {20, EventHandler::WRITE, 2211, 0},
      {30, EventHandler::WRITE, 4096, 0},
      {100, EventHandler::WRITE, 100, 0},
      {0, 0, 0, 0},
  };
  TimePoint start;
  scheduleEvents(&eb, sp[1], events);

  // Schedule a timeout to unregister the handler after the third write
  eb.tryRunAfterDelay(std::bind(&TestHandler::unregisterHandler, &handler), 85);

  // Loop
  eb.loop();
  TimePoint end;

  // The handler should have received the first 3 events,
  // then been unregistered after that.
  ASSERT_EQ(handler.log.size(), 3);
  for (int n = 0; n < 3; ++n) {
    ASSERT_EQ(handler.log[n].events, EventHandler::READ);
    T_CHECK_TIMEOUT(
        start,
        handler.log[n].timestamp,
        std::chrono::milliseconds(events[n].milliseconds));
    ASSERT_EQ(handler.log[n].bytesRead, events[n].length);
    ASSERT_EQ(handler.log[n].bytesWritten, 0);
  }
  T_CHECK_TIMEOUT(
      start, end, std::chrono::milliseconds(events[3].milliseconds));

  // Make sure the data from the last write is still waiting to be read
  size_t bytesRemaining = readUntilEmpty(sp[0]);
  ASSERT_EQ(bytesRemaining, events[3].length);
}

/**
 * Test registering for READ when the socket is immediately readable
 */
TYPED_TEST_P(EventBaseTest, ReadImmediate) {
  auto evbPtr = getEventBase<TypeParam>();
  SKIP_IF(!evbPtr) << "Backend not available";
  folly::EventBase& eb = *evbPtr;
  SocketPair sp;

  // Write some data to the socket so the other end will
  // be immediately readable
  size_t dataLength = 1234;
  writeToFD(sp[1], dataLength);

  // Register for read events
  TestHandler handler(&eb, sp[0]);
  handler.registerHandler(EventHandler::READ | EventHandler::PERSIST);

  // Register a timeout to perform another write
  ScheduledEvent events[] = {
      {10, EventHandler::WRITE, 2345, 0},
      {0, 0, 0, 0},
  };
  TimePoint start;
  scheduleEvents(&eb, sp[1], events);

  // Schedule a timeout to unregister the handler
  eb.tryRunAfterDelay(std::bind(&TestHandler::unregisterHandler, &handler), 20);

  // Loop
  eb.loop();
  TimePoint end;

  ASSERT_EQ(handler.log.size(), 2);

  // There should have been 1 event for immediate readability
  ASSERT_EQ(handler.log[0].events, EventHandler::READ);
  T_CHECK_TIMEOUT(
      start, handler.log[0].timestamp, std::chrono::milliseconds(0));
  ASSERT_EQ(handler.log[0].bytesRead, dataLength);
  ASSERT_EQ(handler.log[0].bytesWritten, 0);

  // There should be another event after the timeout wrote more data
  ASSERT_EQ(handler.log[1].events, EventHandler::READ);
  T_CHECK_TIMEOUT(
      start,
      handler.log[1].timestamp,
      std::chrono::milliseconds(events[0].milliseconds));
  ASSERT_EQ(handler.log[1].bytesRead, events[0].length);
  ASSERT_EQ(handler.log[1].bytesWritten, 0);

  T_CHECK_TIMEOUT(start, end, std::chrono::milliseconds(20));
}

/**
 * Test a WRITE event
 */
TYPED_TEST_P(EventBaseTest, WriteEvent) {
  auto evbPtr = getEventBase<TypeParam>();
  SKIP_IF(!evbPtr) << "Backend not available";
  folly::EventBase& eb = *evbPtr;
  SocketPair sp;

  // Fill up the write buffer before starting
  size_t initialBytesWritten = writeUntilFull(sp[0]);

  // Register for write events
  TestHandler handler(&eb, sp[0]);
  handler.registerHandler(EventHandler::WRITE);

  // Register timeouts to perform two reads
  ScheduledEvent events[] = {
      {10, EventHandler::READ, 0, 0},
      {60, EventHandler::READ, 0, 0},
      {0, 0, 0, 0},
  };
  TimePoint start;
  scheduleEvents(&eb, sp[1], events);

  // Loop
  eb.loop();
  TimePoint end;

  // Since we didn't use the EventHandler::PERSIST flag, the handler should
  // have only been able to write once, then unregistered itself.
  ASSERT_EQ(handler.log.size(), 1);
  ASSERT_EQ(handler.log[0].events, EventHandler::WRITE);
  T_CHECK_TIMEOUT(
      start,
      handler.log[0].timestamp,
      std::chrono::milliseconds(events[0].milliseconds));
  ASSERT_EQ(handler.log[0].bytesRead, 0);
  ASSERT_GT(handler.log[0].bytesWritten, 0);
  T_CHECK_TIMEOUT(
      start, end, std::chrono::milliseconds(events[1].milliseconds));

  ASSERT_EQ(events[0].result, initialBytesWritten);
  ASSERT_EQ(events[1].result, handler.log[0].bytesWritten);
}

/**
 * Test (WRITE | PERSIST)
 */
TYPED_TEST_P(EventBaseTest, WritePersist) {
  auto evbPtr = getEventBase<TypeParam>();
  SKIP_IF(!evbPtr) << "Backend not available";
  folly::EventBase& eb = *evbPtr;
  SocketPair sp;

  // Fill up the write buffer before starting
  size_t initialBytesWritten = writeUntilFull(sp[0]);

  // Register for write events
  TestHandler handler(&eb, sp[0]);
  handler.registerHandler(EventHandler::WRITE | EventHandler::PERSIST);

  // Register several timeouts to read from the socket at several intervals
  ScheduledEvent events[] = {
      {10, EventHandler::READ, 0, 0},
      {40, EventHandler::READ, 0, 0},
      {70, EventHandler::READ, 0, 0},
      {100, EventHandler::READ, 0, 0},
      {0, 0, 0, 0},
  };
  TimePoint start;
  scheduleEvents(&eb, sp[1], events);

  // Schedule a timeout to unregister the handler after the third read
  eb.tryRunAfterDelay(std::bind(&TestHandler::unregisterHandler, &handler), 85);

  // Loop
  eb.loop();
  TimePoint end;

  // The handler should have received the first 3 events,
  // then been unregistered after that.
  ASSERT_EQ(handler.log.size(), 3);
  ASSERT_EQ(events[0].result, initialBytesWritten);
  for (int n = 0; n < 3; ++n) {
    ASSERT_EQ(handler.log[n].events, EventHandler::WRITE);
    T_CHECK_TIMEOUT(
        start,
        handler.log[n].timestamp,
        std::chrono::milliseconds(events[n].milliseconds));
    ASSERT_EQ(handler.log[n].bytesRead, 0);
    ASSERT_GT(handler.log[n].bytesWritten, 0);
    ASSERT_EQ(handler.log[n].bytesWritten, events[n + 1].result);
  }
  T_CHECK_TIMEOUT(
      start, end, std::chrono::milliseconds(events[3].milliseconds));
}

/**
 * Test registering for WRITE when the socket is immediately writable
 */
TYPED_TEST_P(EventBaseTest, WriteImmediate) {
  auto evbPtr = getEventBase<TypeParam>();
  SKIP_IF(!evbPtr) << "Backend not available";
  folly::EventBase& eb = *evbPtr;
  SocketPair sp;

  // Register for write events
  TestHandler handler(&eb, sp[0]);
  handler.registerHandler(EventHandler::WRITE | EventHandler::PERSIST);

  // Register a timeout to perform a read
  ScheduledEvent events[] = {
      {10, EventHandler::READ, 0, 0},
      {0, 0, 0, 0},
  };
  TimePoint start;
  scheduleEvents(&eb, sp[1], events);

  // Schedule a timeout to unregister the handler
  int64_t unregisterTimeout = 40;
  eb.tryRunAfterDelay(
      std::bind(&TestHandler::unregisterHandler, &handler), unregisterTimeout);

  // Loop
  eb.loop();
  TimePoint end;

  ASSERT_EQ(handler.log.size(), 2);

  // Since the socket buffer was initially empty,
  // there should have been 1 event for immediate writability
  ASSERT_EQ(handler.log[0].events, EventHandler::WRITE);
  T_CHECK_TIMEOUT(
      start, handler.log[0].timestamp, std::chrono::milliseconds(0));
  ASSERT_EQ(handler.log[0].bytesRead, 0);
  ASSERT_GT(handler.log[0].bytesWritten, 0);

  // There should be another event after the timeout wrote more data
  ASSERT_EQ(handler.log[1].events, EventHandler::WRITE);
  T_CHECK_TIMEOUT(
      start,
      handler.log[1].timestamp,
      std::chrono::milliseconds(events[0].milliseconds));
  ASSERT_EQ(handler.log[1].bytesRead, 0);
  ASSERT_GT(handler.log[1].bytesWritten, 0);

  T_CHECK_TIMEOUT(start, end, std::chrono::milliseconds(unregisterTimeout));
}

/**
 * Test (READ | WRITE) when the socket becomes readable first
 */
TYPED_TEST_P(EventBaseTest, ReadWrite) {
  auto evbPtr = getEventBase<TypeParam>();
  SKIP_IF(!evbPtr) << "Backend not available";
  folly::EventBase& eb = *evbPtr;
  SocketPair sp;

  // Fill up the write buffer before starting
  size_t sock0WriteLength = writeUntilFull(sp[0]);

  // Register for read and write events
  TestHandler handler(&eb, sp[0]);
  handler.registerHandler(EventHandler::READ_WRITE);

  // Register timeouts to perform a write then a read.
  ScheduledEvent events[] = {
      {10, EventHandler::WRITE, 2345, 0},
      {40, EventHandler::READ, 0, 0},
      {0, 0, 0, 0},
  };
  TimePoint start;
  scheduleEvents(&eb, sp[1], events);

  // Loop
  eb.loop();
  TimePoint end;

  // Since we didn't use the EventHandler::PERSIST flag, the handler should
  // have only noticed readability, then unregistered itself.  Check that only
  // one event was logged.
  ASSERT_EQ(handler.log.size(), 1);
  ASSERT_EQ(handler.log[0].events, EventHandler::READ);
  T_CHECK_TIMEOUT(
      start,
      handler.log[0].timestamp,
      std::chrono::milliseconds(events[0].milliseconds));
  ASSERT_EQ(handler.log[0].bytesRead, events[0].length);
  ASSERT_EQ(handler.log[0].bytesWritten, 0);
  ASSERT_EQ(events[1].result, sock0WriteLength);
  T_CHECK_TIMEOUT(
      start, end, std::chrono::milliseconds(events[1].milliseconds));
}

/**
 * Test (READ | WRITE) when the socket becomes writable first
 */
TYPED_TEST_P(EventBaseTest, WriteRead) {
  auto evbPtr = getEventBase<TypeParam>();
  SKIP_IF(!evbPtr) << "Backend not available";
  folly::EventBase& eb = *evbPtr;
  SocketPair sp;

  // Fill up the write buffer before starting
  size_t sock0WriteLength = writeUntilFull(sp[0]);

  // Register for read and write events
  TestHandler handler(&eb, sp[0]);
  handler.registerHandler(EventHandler::READ_WRITE);

  // Register timeouts to perform a read then a write.
  size_t sock1WriteLength = 2345;
  ScheduledEvent events[] = {
      {10, EventHandler::READ, 0, 0},
      {40, EventHandler::WRITE, sock1WriteLength, 0},
      {0, 0, 0, 0},
  };
  TimePoint start;
  scheduleEvents(&eb, sp[1], events);

  // Loop
  eb.loop();
  TimePoint end;

  // Since we didn't use the EventHandler::PERSIST flag, the handler should
  // have only noticed writability, then unregistered itself.  Check that only
  // one event was logged.
  ASSERT_EQ(handler.log.size(), 1);
  ASSERT_EQ(handler.log[0].events, EventHandler::WRITE);
  T_CHECK_TIMEOUT(
      start,
      handler.log[0].timestamp,
      std::chrono::milliseconds(events[0].milliseconds));
  ASSERT_EQ(handler.log[0].bytesRead, 0);
  ASSERT_GT(handler.log[0].bytesWritten, 0);
  ASSERT_EQ(events[0].result, sock0WriteLength);
  ASSERT_EQ(events[1].result, sock1WriteLength);
  T_CHECK_TIMEOUT(
      start, end, std::chrono::milliseconds(events[1].milliseconds));

  // Make sure the written data is still waiting to be read.
  size_t bytesRemaining = readUntilEmpty(sp[0]);
  ASSERT_EQ(bytesRemaining, events[1].length);
}

/**
 * Test (READ | WRITE) when the socket becomes readable and writable
 * at the same time.
 */
TYPED_TEST_P(EventBaseTest, ReadWriteSimultaneous) {
  auto evbPtr = getEventBase<TypeParam>();
  SKIP_IF(!evbPtr) << "Backend not available";
  folly::EventBase& eb = *evbPtr;
  SocketPair sp;

  // Fill up the write buffer before starting
  size_t sock0WriteLength = writeUntilFull(sp[0]);

  // Register for read and write events
  TestHandler handler(&eb, sp[0]);
  handler.registerHandler(EventHandler::READ_WRITE);

  // Register a timeout to perform a read and write together
  ScheduledEvent events[] = {
      {10, EventHandler::READ | EventHandler::WRITE, 0, 0},
      {0, 0, 0, 0},
  };
  TimePoint start;
  scheduleEvents(&eb, sp[1], events);

  // Loop
  eb.loop();
  TimePoint end;

  // It's not strictly required that the EventBase register us about both
  // events in the same call or thw read/write notifications are delievered at
  // the same. So, it's possible that if the EventBase implementation changes
  // this test could start failing, and it wouldn't be considered breaking the
  // API.  However for now it's nice to exercise this code path.
  ASSERT_EQ(handler.log.size(), 1);
  if (handler.log[0].events & EventHandler::READ) {
    ASSERT_EQ(handler.log[0].bytesRead, sock0WriteLength);
    ASSERT_GT(handler.log[0].bytesWritten, 0);
  }
  T_CHECK_TIMEOUT(
      start,
      handler.log[0].timestamp,
      std::chrono::milliseconds(events[0].milliseconds));
  T_CHECK_TIMEOUT(
      start, end, std::chrono::milliseconds(events[0].milliseconds));
}

/**
 * Test (READ | WRITE | PERSIST)
 */
TYPED_TEST_P(EventBaseTest, ReadWritePersist) {
  auto evbPtr = getEventBase<TypeParam>();
  SKIP_IF(!evbPtr) << "Backend not available";
  folly::EventBase& eb = *evbPtr;
  SocketPair sp;

  // Register for read and write events
  TestHandler handler(&eb, sp[0]);
  handler.registerHandler(
      EventHandler::READ | EventHandler::WRITE | EventHandler::PERSIST);

  // Register timeouts to perform several reads and writes
  ScheduledEvent events[] = {
      {10, EventHandler::WRITE, 2345, 0},
      {20, EventHandler::READ, 0, 0},
      {35, EventHandler::WRITE, 200, 0},
      {45, EventHandler::WRITE, 15, 0},
      {55, EventHandler::READ, 0, 0},
      {120, EventHandler::WRITE, 2345, 0},
      {0, 0, 0, 0},
  };
  TimePoint start;
  scheduleEvents(&eb, sp[1], events);

  // Schedule a timeout to unregister the handler
  eb.tryRunAfterDelay(std::bind(&TestHandler::unregisterHandler, &handler), 80);

  // Loop
  eb.loop();
  TimePoint end;

  ASSERT_EQ(handler.log.size(), 6);

  // Since we didn't fill up the write buffer immediately, there should
  // be an immediate event for writability.
  ASSERT_EQ(handler.log[0].events, EventHandler::WRITE);
  T_CHECK_TIMEOUT(
      start, handler.log[0].timestamp, std::chrono::milliseconds(0));
  ASSERT_EQ(handler.log[0].bytesRead, 0);
  ASSERT_GT(handler.log[0].bytesWritten, 0);

  // Events 1 through 5 should correspond to the scheduled events
  for (int n = 1; n < 6; ++n) {
    ScheduledEvent* event = &events[n - 1];
    T_CHECK_TIMEOUT(
        start,
        handler.log[n].timestamp,
        std::chrono::milliseconds(event->milliseconds));
    if (event->events == EventHandler::READ) {
      ASSERT_EQ(handler.log[n].events, EventHandler::WRITE);
      ASSERT_EQ(handler.log[n].bytesRead, 0);
      ASSERT_GT(handler.log[n].bytesWritten, 0);
    } else {
      ASSERT_EQ(handler.log[n].events, EventHandler::READ);
      ASSERT_EQ(handler.log[n].bytesRead, event->length);
      ASSERT_EQ(handler.log[n].bytesWritten, 0);
    }
  }

  // The timeout should have unregistered the handler before the last write.
  // Make sure that data is still waiting to be read
  size_t bytesRemaining = readUntilEmpty(sp[0]);
  ASSERT_EQ(bytesRemaining, events[5].length);
}

namespace {
class PartialReadHandler : public TestHandler {
 public:
  PartialReadHandler(EventBase* eventBase, int fd, size_t readLength)
      : TestHandler(eventBase, fd), fd_(fd), readLength_(readLength) {}

  void handlerReady(uint16_t events) noexcept override {
    assert(events == EventHandler::READ);
    ssize_t bytesRead = readFromFD(fd_, readLength_);
    log.emplace_back(events, bytesRead, 0);
  }

 private:
  int fd_;
  size_t readLength_;
};
} // namespace

/**
 * Test reading only part of the available data when a read event is fired.
 * When PERSIST is used, make sure the handler gets notified again the next
 * time around the loop.
 */
TYPED_TEST_P(EventBaseTest, ReadPartial) {
  auto evbPtr = getEventBase<TypeParam>();
  SKIP_IF(!evbPtr) << "Backend not available";
  folly::EventBase& eb = *evbPtr;
  SocketPair sp;

  // Register for read events
  size_t readLength = 100;
  PartialReadHandler handler(&eb, sp[0], readLength);
  handler.registerHandler(EventHandler::READ | EventHandler::PERSIST);

  // Register a timeout to perform a single write,
  // with more data than PartialReadHandler will read at once
  ScheduledEvent events[] = {
      {10, EventHandler::WRITE, (3 * readLength) + (readLength / 2), 0},
      {0, 0, 0, 0},
  };
  TimePoint start;
  scheduleEvents(&eb, sp[1], events);

  // Schedule a timeout to unregister the handler
  eb.tryRunAfterDelay(std::bind(&TestHandler::unregisterHandler, &handler), 30);

  // Loop
  eb.loop();
  TimePoint end;

  ASSERT_EQ(handler.log.size(), 4);

  // The first 3 invocations should read readLength bytes each
  for (int n = 0; n < 3; ++n) {
    ASSERT_EQ(handler.log[n].events, EventHandler::READ);
    T_CHECK_TIMEOUT(
        start,
        handler.log[n].timestamp,
        std::chrono::milliseconds(events[0].milliseconds));
    ASSERT_EQ(handler.log[n].bytesRead, readLength);
    ASSERT_EQ(handler.log[n].bytesWritten, 0);
  }
  // The last read only has readLength/2 bytes
  ASSERT_EQ(handler.log[3].events, EventHandler::READ);
  T_CHECK_TIMEOUT(
      start,
      handler.log[3].timestamp,
      std::chrono::milliseconds(events[0].milliseconds));
  ASSERT_EQ(handler.log[3].bytesRead, readLength / 2);
  ASSERT_EQ(handler.log[3].bytesWritten, 0);
}

namespace {
class PartialWriteHandler : public TestHandler {
 public:
  PartialWriteHandler(EventBase* eventBase, int fd, size_t writeLength)
      : TestHandler(eventBase, fd), fd_(fd), writeLength_(writeLength) {}

  void handlerReady(uint16_t events) noexcept override {
    assert(events == EventHandler::WRITE);
    ssize_t bytesWritten = writeToFD(fd_, writeLength_);
    log.emplace_back(events, 0, bytesWritten);
  }

 private:
  int fd_;
  size_t writeLength_;
};
} // namespace

/**
 * Test writing without completely filling up the write buffer when the fd
 * becomes writable.  When PERSIST is used, make sure the handler gets
 * notified again the next time around the loop.
 */
TYPED_TEST_P(EventBaseTest, WritePartial) {
  auto evbPtr = getEventBase<TypeParam>();
  SKIP_IF(!evbPtr) << "Backend not available";
  folly::EventBase& eb = *evbPtr;
  SocketPair sp;

  // Fill up the write buffer before starting
  size_t initialBytesWritten = writeUntilFull(sp[0]);

  // Register for write events
  size_t writeLength = 100;
  PartialWriteHandler handler(&eb, sp[0], writeLength);
  handler.registerHandler(EventHandler::WRITE | EventHandler::PERSIST);

  // Register a timeout to read, so that more data can be written
  ScheduledEvent events[] = {
      {10, EventHandler::READ, 0, 0},
      {0, 0, 0, 0},
  };
  TimePoint start;
  scheduleEvents(&eb, sp[1], events);

  // Schedule a timeout to unregister the handler
  eb.tryRunAfterDelay(std::bind(&TestHandler::unregisterHandler, &handler), 30);

  // Loop
  eb.loop();
  TimePoint end;

  // Depending on how big the socket buffer is, there will be multiple writes
  // Only check the first 5
  int numChecked = 5;
  ASSERT_GE(handler.log.size(), numChecked);
  ASSERT_EQ(events[0].result, initialBytesWritten);

  // The first 3 invocations should read writeLength bytes each
  for (int n = 0; n < numChecked; ++n) {
    ASSERT_EQ(handler.log[n].events, EventHandler::WRITE);
    T_CHECK_TIMEOUT(
        start,
        handler.log[n].timestamp,
        std::chrono::milliseconds(events[0].milliseconds));
    ASSERT_EQ(handler.log[n].bytesRead, 0);
    ASSERT_EQ(handler.log[n].bytesWritten, writeLength);
  }
}

namespace {
class DestroyHandler : public AsyncTimeout {
 public:
  DestroyHandler(EventBase* eb, EventHandler* h)
      : AsyncTimeout(eb), handler_(h) {}

  void timeoutExpired() noexcept override { delete handler_; }

 private:
  EventHandler* handler_;
};
} // namespace

/**
 * Test destroying a registered EventHandler
 */
TYPED_TEST_P(EventBaseTest, DestroyingHandler) {
  auto evbPtr = getEventBase<TypeParam>();
  SKIP_IF(!evbPtr) << "Backend not available";
  folly::EventBase& eb = *evbPtr;
  SocketPair sp;

  // Fill up the write buffer before starting
  size_t initialBytesWritten = writeUntilFull(sp[0]);

  // Register for write events
  TestHandler* handler = new TestHandler(&eb, sp[0]);
  handler->registerHandler(EventHandler::WRITE | EventHandler::PERSIST);

  // After 10ms, read some data, so that the handler
  // will be notified that it can write.
  eb.tryRunAfterDelay(
      std::bind(checkReadUntilEmpty, sp[1], initialBytesWritten), 10);

  // Start a timer to destroy the handler after 25ms
  // This mainly just makes sure the code doesn't break or assert
  DestroyHandler dh(&eb, handler);
  dh.scheduleTimeout(25);

  TimePoint start;
  eb.loop();
  TimePoint end;

  // Make sure the EventHandler was uninstalled properly when it was
  // destroyed, and the EventBase loop exited
  T_CHECK_TIMEOUT(start, end, std::chrono::milliseconds(25));

  // Make sure that the handler wrote data to the socket
  // before it was destroyed
  size_t bytesRemaining = readUntilEmpty(sp[1]);
  ASSERT_GT(bytesRemaining, 0);
}

///////////////////////////////////////////////////////////////////////////
// Tests for timeout events
///////////////////////////////////////////////////////////////////////////

TYPED_TEST_P(EventBaseTest, RunAfterDelay) {
  auto evbPtr = getEventBase<TypeParam>();
  SKIP_IF(!evbPtr) << "Backend not available";
  folly::EventBase& eb = *evbPtr;

  TimePoint timestamp1(false);
  TimePoint timestamp2(false);
  TimePoint timestamp3(false);
  auto fn1 = std::bind(&TimePoint::reset, &timestamp1);
  auto fn2 = std::bind(&TimePoint::reset, &timestamp2);
  auto fn3 = std::bind(&TimePoint::reset, &timestamp3);

  TimePoint start;
  eb.tryRunAfterDelay(std::move(fn1), 10);
  eb.tryRunAfterDelay(std::move(fn2), 20);
  eb.tryRunAfterDelay(std::move(fn3), 40);

  eb.loop();
  TimePoint end;

  T_CHECK_TIMEOUT(start, timestamp1, std::chrono::milliseconds(10));
  T_CHECK_TIMEOUT(start, timestamp2, std::chrono::milliseconds(20));
  T_CHECK_TIMEOUT(start, timestamp3, std::chrono::milliseconds(40));
  T_CHECK_TIMEOUT(start, end, std::chrono::milliseconds(40));
}

/**
 * Test the behavior of tryRunAfterDelay() when some timeouts are
 * still scheduled when the EventBase is destroyed.
 */
TYPED_TEST_P(EventBaseTest, RunAfterDelayDestruction) {
  TimePoint timestamp1(false);
  TimePoint timestamp2(false);
  TimePoint timestamp3(false);
  TimePoint timestamp4(false);
  TimePoint start(false);
  TimePoint end(false);

  {
    auto evbPtr = getEventBase<TypeParam>();
    SKIP_IF(!evbPtr) << "Backend not available";
    folly::EventBase& eb = *evbPtr;
    start.reset();

    // Run two normal timeouts
    eb.tryRunAfterDelay(std::bind(&TimePoint::reset, &timestamp1), 10);
    eb.tryRunAfterDelay(std::bind(&TimePoint::reset, &timestamp2), 20);

    // Schedule a timeout to stop the event loop after 40ms
    eb.tryRunAfterDelay(std::bind(&EventBase::terminateLoopSoon, &eb), 40);

    // Schedule 2 timeouts that would fire after the event loop stops
    eb.tryRunAfterDelay(std::bind(&TimePoint::reset, &timestamp3), 80);
    eb.tryRunAfterDelay(std::bind(&TimePoint::reset, &timestamp4), 160);

    eb.loop();
    end.reset();
  }

  T_CHECK_TIMEOUT(start, timestamp1, std::chrono::milliseconds(10));
  T_CHECK_TIMEOUT(start, timestamp2, std::chrono::milliseconds(20));
  T_CHECK_TIMEOUT(start, end, std::chrono::milliseconds(40));

  ASSERT_TRUE(timestamp3.isUnset());
  ASSERT_TRUE(timestamp4.isUnset());

  // Ideally this test should be run under valgrind to ensure that no
  // memory is leaked.
}

namespace {
class TestTimeout : public AsyncTimeout {
 public:
  explicit TestTimeout(EventBase* eventBase)
      : AsyncTimeout(eventBase), timestamp(false) {}

  void timeoutExpired() noexcept override { timestamp.reset(); }

  TimePoint timestamp;
};
} // namespace

TYPED_TEST_P(EventBaseTest, BasicTimeouts) {
  auto evbPtr = getEventBase<TypeParam>();
  SKIP_IF(!evbPtr) << "Backend not available";
  folly::EventBase& eb = *evbPtr;

  TestTimeout t1(&eb);
  TestTimeout t2(&eb);
  TestTimeout t3(&eb);
  TimePoint start;
  t1.scheduleTimeout(10);
  t2.scheduleTimeout(20);
  t3.scheduleTimeout(40);

  eb.loop();
  TimePoint end;

  T_CHECK_TIMEOUT(start, t1.timestamp, std::chrono::milliseconds(10));
  T_CHECK_TIMEOUT(start, t2.timestamp, std::chrono::milliseconds(20));
  T_CHECK_TIMEOUT(start, t3.timestamp, std::chrono::milliseconds(40));
  T_CHECK_TIMEOUT(start, end, std::chrono::milliseconds(40));
}

namespace {
class ReschedulingTimeout : public AsyncTimeout {
 public:
  ReschedulingTimeout(EventBase* evb, const std::vector<uint32_t>& timeouts)
      : AsyncTimeout(evb), timeouts_(timeouts), iterator_(timeouts_.begin()) {}

  void start() { reschedule(); }

  void timeoutExpired() noexcept override {
    timestamps.emplace_back();
    reschedule();
  }

  void reschedule() {
    if (iterator_ != timeouts_.end()) {
      uint32_t timeout = *iterator_;
      ++iterator_;
      scheduleTimeout(timeout);
    }
  }

  std::vector<TimePoint> timestamps;

 private:
  std::vector<uint32_t> timeouts_;
  std::vector<uint32_t>::const_iterator iterator_;
};
} // namespace

/**
 * Test rescheduling the same timeout multiple times
 */
TYPED_TEST_P(EventBaseTest, ReuseTimeout) {
  auto evbPtr = getEventBase<TypeParam>();
  SKIP_IF(!evbPtr) << "Backend not available";
  folly::EventBase& eb = *evbPtr;

  std::vector<uint32_t> timeouts;
  timeouts.push_back(10);
  timeouts.push_back(30);
  timeouts.push_back(15);

  ReschedulingTimeout t(&eb, timeouts);
  TimePoint start;
  t.start();
  eb.loop();
  TimePoint end;

  // Use a higher tolerance than usual.  We're waiting on 3 timeouts
  // consecutively.  In general, each timeout may go over by a few
  // milliseconds, and we're tripling this error by witing on 3 timeouts.
  std::chrono::milliseconds tolerance{6};

  ASSERT_EQ(timeouts.size(), t.timestamps.size());
  uint32_t total = 0;
  for (size_t n = 0; n < timeouts.size(); ++n) {
    total += timeouts[n];
    T_CHECK_TIMEOUT(
        start, t.timestamps[n], std::chrono::milliseconds(total), tolerance);
  }
  T_CHECK_TIMEOUT(start, end, std::chrono::milliseconds(total), tolerance);
}

/**
 * Test rescheduling a timeout before it has fired
 */
TYPED_TEST_P(EventBaseTest, RescheduleTimeout) {
  auto evbPtr = getEventBase<TypeParam>();
  SKIP_IF(!evbPtr) << "Backend not available";
  folly::EventBase& eb = *evbPtr;

  TestTimeout t1(&eb);
  TestTimeout t2(&eb);
  TestTimeout t3(&eb);

  TimePoint start;
  t1.scheduleTimeout(15);
  t2.scheduleTimeout(30);
  t3.scheduleTimeout(30);

  // after 10ms, reschedule t2 to run sooner than originally scheduled
  eb.tryRunAfterDelay([&] { t2.scheduleTimeout(10); }, 10);
  // after 10ms, reschedule t3 to run later than originally scheduled
  eb.tryRunAfterDelay([&] { t3.scheduleTimeout(40); }, 10);

  eb.loop();
  TimePoint end;

  T_CHECK_TIMEOUT(start, t1.timestamp, std::chrono::milliseconds(15));
  T_CHECK_TIMEOUT(start, t2.timestamp, std::chrono::milliseconds(20));
  T_CHECK_TIMEOUT(start, t3.timestamp, std::chrono::milliseconds(50));
  T_CHECK_TIMEOUT(start, end, std::chrono::milliseconds(50));
}

/**
 * Test cancelling a timeout
 */
TYPED_TEST_P(EventBaseTest, CancelTimeout) {
  auto evbPtr = getEventBase<TypeParam>();
  SKIP_IF(!evbPtr) << "Backend not available";
  folly::EventBase& eb = *evbPtr;

  std::vector<uint32_t> timeouts;
  timeouts.push_back(10);
  timeouts.push_back(30);
  timeouts.push_back(25);

  ReschedulingTimeout t(&eb, timeouts);
  TimePoint start;
  t.start();
  eb.tryRunAfterDelay(std::bind(&AsyncTimeout::cancelTimeout, &t), 50);

  eb.loop();
  TimePoint end;

  ASSERT_EQ(t.timestamps.size(), 2);
  T_CHECK_TIMEOUT(start, t.timestamps[0], std::chrono::milliseconds(10));
  T_CHECK_TIMEOUT(start, t.timestamps[1], std::chrono::milliseconds(40));
  T_CHECK_TIMEOUT(start, end, std::chrono::milliseconds(50));
}

namespace {
class DestroyTimeout : public AsyncTimeout {
 public:
  DestroyTimeout(EventBase* eb, AsyncTimeout* t)
      : AsyncTimeout(eb), timeout_(t) {}

  void timeoutExpired() noexcept override { delete timeout_; }

 private:
  AsyncTimeout* timeout_;
};
} // namespace

/**
 * Test destroying a scheduled timeout object
 */
TYPED_TEST_P(EventBaseTest, DestroyingTimeout) {
  auto evbPtr = getEventBase<TypeParam>();
  SKIP_IF(!evbPtr) << "Backend not available";
  folly::EventBase& eb = *evbPtr;

  TestTimeout* t1 = new TestTimeout(&eb);
  TimePoint start;
  t1->scheduleTimeout(30);

  DestroyTimeout dt(&eb, t1);
  dt.scheduleTimeout(10);

  eb.loop();
  TimePoint end;

  T_CHECK_TIMEOUT(start, end, std::chrono::milliseconds(10));
}

/**
 * Test the scheduled executor impl
 */
TYPED_TEST_P(EventBaseTest, ScheduledFn) {
  auto evbPtr = getEventBase<TypeParam>();
  SKIP_IF(!evbPtr) << "Backend not available";
  folly::EventBase& eb = *evbPtr;

  TimePoint timestamp1(false);
  TimePoint timestamp2(false);
  TimePoint timestamp3(false);
  auto fn1 = std::bind(&TimePoint::reset, &timestamp1);
  auto fn2 = std::bind(&TimePoint::reset, &timestamp2);
  auto fn3 = std::bind(&TimePoint::reset, &timestamp3);
  TimePoint start;
  eb.schedule(std::move(fn1), std::chrono::milliseconds(9));
  eb.schedule(std::move(fn2), std::chrono::milliseconds(19));
  eb.schedule(std::move(fn3), std::chrono::milliseconds(39));

  eb.loop();
  TimePoint end;

  T_CHECK_TIMEOUT(start, timestamp1, std::chrono::milliseconds(9));
  T_CHECK_TIMEOUT(start, timestamp2, std::chrono::milliseconds(19));
  T_CHECK_TIMEOUT(start, timestamp3, std::chrono::milliseconds(39));
  T_CHECK_TIMEOUT(start, end, std::chrono::milliseconds(39));
}

TYPED_TEST_P(EventBaseTest, ScheduledFnAt) {
  auto evbPtr = getEventBase<TypeParam>();
  SKIP_IF(!evbPtr) << "Backend not available";
  folly::EventBase& eb = *evbPtr;

  TimePoint timestamp0(false);
  TimePoint timestamp1(false);
  TimePoint timestamp2(false);
  TimePoint timestamp3(false);
  auto fn0 = std::bind(&TimePoint::reset, &timestamp0);
  auto fn1 = std::bind(&TimePoint::reset, &timestamp1);
  auto fn2 = std::bind(&TimePoint::reset, &timestamp2);
  auto fn3 = std::bind(&TimePoint::reset, &timestamp3);
  TimePoint start;
  eb.scheduleAt(fn0, eb.now() - std::chrono::milliseconds(5));
  eb.scheduleAt(fn1, eb.now() + std::chrono::milliseconds(9));
  eb.scheduleAt(fn2, eb.now() + std::chrono::milliseconds(19));
  eb.scheduleAt(fn3, eb.now() + std::chrono::milliseconds(39));

  TimePoint loopStart;
  eb.loop();
  TimePoint end;

  // Even though we asked to schedule the first function in the past,
  // in practice it doesn't run until after 1 iteration of the HHWheelTimer tick
  // interval.
  T_CHECK_TIMEOUT(start, timestamp0, eb.timer().getTickInterval());

  T_CHECK_TIMEOUT(start, timestamp1, std::chrono::milliseconds(9));
  T_CHECK_TIMEOUT(start, timestamp2, std::chrono::milliseconds(19));
  T_CHECK_TIMEOUT(start, timestamp3, std::chrono::milliseconds(39));
  T_CHECK_TIMEOUT(start, end, std::chrono::milliseconds(39));
}

///////////////////////////////////////////////////////////////////////////
// Test for runInThreadTestFunc()
///////////////////////////////////////////////////////////////////////////

namespace {

struct RunInThreadData {
  RunInThreadData(
      folly::EventBaseBackendBase::FactoryFunc backendFactory,
      int numThreads,
      int opsPerThread_)
      : evb(folly::EventBase::Options().setBackendFactory(
            std::move(backendFactory))),
        opsPerThread(opsPerThread_),
        opsToGo(numThreads * opsPerThread) {}

  EventBase evb;
  std::deque<std::pair<int, int>> values;

  int opsPerThread;
  int opsToGo;
};

struct RunInThreadArg {
  RunInThreadArg(RunInThreadData* data_, int threadId, int value_)
      : data(data_), thread(threadId), value(value_) {}

  RunInThreadData* data;
  int thread;
  int value;
};

static inline void runInThreadTestFunc(RunInThreadArg* arg) {
  arg->data->values.emplace_back(arg->thread, arg->value);
  RunInThreadData* data = arg->data;
  delete arg;

  if (--data->opsToGo == 0) {
    // Break out of the event base loop if we are the last thread running
    data->evb.terminateLoopSoon();
  }
}

} // namespace

TYPED_TEST_P(EventBaseTest, RunInThread) {
  constexpr uint32_t numThreads = 50;
  constexpr uint32_t opsPerThread = 100;
  auto backend = TypeParam::getBackend();
  SKIP_IF(!backend) << "Backend not available";
  RunInThreadData data(
      [] { return TypeParam::getBackend(); }, numThreads, opsPerThread);

  std::deque<std::thread> threads;
  SCOPE_EXIT {
    // Wait on all of the threads.
    for (auto& thread : threads) {
      thread.join();
    }
  };

  for (uint32_t i = 0; i < numThreads; ++i) {
    threads.emplace_back([i, &data] {
      for (int n = 0; n < data.opsPerThread; ++n) {
        RunInThreadArg* arg = new RunInThreadArg(&data, i, n);
        data.evb.runInEventBaseThread(runInThreadTestFunc, arg);
        usleep(10);
      }
    });
  }

  // Add a timeout event to run after 3 seconds.
  // Otherwise loop() will return immediately since there are no events to run.
  // Once the last thread exits, it will stop the loop().  However, this
  // timeout also stops the loop in case there is a bug performing the normal
  // stop.
  data.evb.tryRunAfterDelay(
      std::bind(&EventBase::terminateLoopSoon, &data.evb), 3000);

  TimePoint start;
  data.evb.loop();
  TimePoint end;

  // Verify that the loop exited because all threads finished and requested it
  // to stop.  This should happen much sooner than the 3 second timeout.
  // Assert that it happens in under a second.  (This is still tons of extra
  // padding.)

  auto timeTaken = std::chrono::duration_cast<std::chrono::milliseconds>(
      end.getTime() - start.getTime());
  ASSERT_LT(timeTaken.count(), 1000);
  VLOG(11) << "Time taken: " << timeTaken.count();

  // Verify that we have all of the events from every thread
  int expectedValues[numThreads];
  for (uint32_t n = 0; n < numThreads; ++n) {
    expectedValues[n] = 0;
  }
  for (const auto& dataValue : data.values) {
    int threadID = dataValue.first;
    int value = dataValue.second;
    ASSERT_EQ(expectedValues[threadID], value);
    ++expectedValues[threadID];
  }
  for (uint32_t n = 0; n < numThreads; ++n) {
    ASSERT_EQ(expectedValues[n], opsPerThread);
  }
}

//  This test simulates some calls, and verifies that the waiting happens by
//  triggering what otherwise would be race conditions, and trying to detect
//  whether any of the race conditions happened.
TYPED_TEST_P(EventBaseTest, RunInEventBaseThreadAndWait) {
  const size_t c = 256;
  std::vector<std::unique_ptr<EventBase>> evbs;
  for (size_t i = 0; i < c; ++i) {
    auto evbPtr = getEventBase<TypeParam>();
    SKIP_IF(!evbPtr) << "Backend not available";
    evbs.push_back(std::move(evbPtr));
  }

  std::vector<std::unique_ptr<std::atomic<size_t>>> atoms(c);
  for (size_t i = 0; i < c; ++i) {
    auto& atom = atoms.at(i);
    atom = std::make_unique<std::atomic<size_t>>(0);
  }
  std::vector<std::thread> threads;
  for (size_t i = 0; i < c; ++i) {
    threads.emplace_back([&atoms, i, evb = std::move(evbs[i])] {
      folly::EventBase& eb = *evb;
      auto& atom = *atoms.at(i);
      auto ebth = std::thread([&] { eb.loopForever(); });
      eb.waitUntilRunning();
      eb.runInEventBaseThreadAndWait([&] {
        size_t x = 0;
        atom.compare_exchange_weak(
            x, 1, std::memory_order_release, std::memory_order_relaxed);
      });
      size_t x = 0;
      atom.compare_exchange_weak(
          x, 2, std::memory_order_release, std::memory_order_relaxed);
      eb.terminateLoopSoon();
      ebth.join();
    });
  }
  for (size_t i = 0; i < c; ++i) {
    auto& th = threads.at(i);
    th.join();
  }
  size_t sum = 0;
  for (auto& atom : atoms) {
    sum += *atom;
  }
  EXPECT_EQ(c, sum);
}

TYPED_TEST_P(EventBaseTest, RunImmediatelyOrRunInEventBaseThreadAndWaitCross) {
  auto evbPtr = getEventBase<TypeParam>();
  SKIP_IF(!evbPtr) << "Backend not available";
  folly::EventBase& eb = *evbPtr;
  std::thread th(&EventBase::loopForever, &eb);
  SCOPE_EXIT {
    eb.terminateLoopSoon();
    th.join();
  };
  auto mutated = false;
  eb.runImmediatelyOrRunInEventBaseThreadAndWait([&] { mutated = true; });
  EXPECT_TRUE(mutated);
}

TYPED_TEST_P(EventBaseTest, RunImmediatelyOrRunInEventBaseThreadAndWaitWithin) {
  auto evbPtr = getEventBase<TypeParam>();
  SKIP_IF(!evbPtr) << "Backend not available";
  folly::EventBase& eb = *evbPtr;
  std::thread th(&EventBase::loopForever, &eb);
  SCOPE_EXIT {
    eb.terminateLoopSoon();
    th.join();
  };
  eb.runInEventBaseThreadAndWait([&] {
    auto mutated = false;
    eb.runImmediatelyOrRunInEventBaseThreadAndWait([&] { mutated = true; });
    EXPECT_TRUE(mutated);
  });
}

TYPED_TEST_P(
    EventBaseTest, RunImmediatelyOrRunInEventBaseThreadAndWaitNotLooping) {
  auto evbPtr = getEventBase<TypeParam>();
  SKIP_IF(!evbPtr) << "Backend not available";
  folly::EventBase& eb = *evbPtr;
  auto mutated = false;
  eb.runImmediatelyOrRunInEventBaseThreadAndWait([&] { mutated = true; });
  EXPECT_TRUE(mutated);
}

TYPED_TEST_P(EventBaseTest, RunImmediatelyOrRunInEventBaseThreadCross) {
  auto evbPtr = getEventBase<TypeParam>();
  SKIP_IF(!evbPtr) << "Backend not available";
  folly::EventBase& eb = *evbPtr;
  std::thread th(&EventBase::loopForever, &eb);
  SCOPE_EXIT {
    eb.terminateLoopSoon();
    th.join();
  };
  // wait for loop to start
  eb.runInEventBaseThreadAndWait([] {});
  Baton<> baton1, baton2;
  EXPECT_FALSE(eb.isInEventBaseThread());

  eb.runImmediatelyOrRunInEventBaseThread([&] {
    baton1.wait();
    baton2.post();
  });
  EXPECT_FALSE(baton2.ready());
  baton1.post();
  EXPECT_TRUE(baton2.try_wait_for(std::chrono::milliseconds(100)));
}

TYPED_TEST_P(EventBaseTest, RunImmediatelyOrRunInEventBaseThreadNotLooping) {
  auto evbPtr = getEventBase<TypeParam>();
  SKIP_IF(!evbPtr) << "Backend not available";
  folly::EventBase& eb = *evbPtr;
  Baton<> baton;
  eb.runImmediatelyOrRunInEventBaseThread([&] { baton.post(); });
  EXPECT_TRUE(baton.ready());
}

///////////////////////////////////////////////////////////////////////////
// Tests for runInLoop()
///////////////////////////////////////////////////////////////////////////

namespace {
class CountedLoopCallback : public EventBase::LoopCallback {
 public:
  CountedLoopCallback(
      EventBase* eventBase,
      unsigned int count,
      std::function<void()> action = std::function<void()>())
      : eventBase_(eventBase), count_(count), action_(action) {}

  void runLoopCallback() noexcept override {
    --count_;
    if (count_ > 0) {
      eventBase_->runInLoop(this);
    } else if (action_) {
      action_();
    }
  }

  unsigned int getCount() const { return count_; }

 private:
  EventBase* eventBase_;
  unsigned int count_;
  std::function<void()> action_;
};
} // namespace

// Test that EventBase::loop() doesn't exit while there are
// still LoopCallbacks remaining to be invoked.
TYPED_TEST_P(EventBaseTest, RepeatedRunInLoop) {
  auto evbPtr = getEventBase<TypeParam>();
  SKIP_IF(!evbPtr) << "Backend not available";
  folly::EventBase& eventBase = *evbPtr;

  CountedLoopCallback c(&eventBase, 10);
  eventBase.runInLoop(&c);
  // The callback shouldn't have run immediately
  ASSERT_EQ(c.getCount(), 10);
  eventBase.loop();

  // loop() should loop until the CountedLoopCallback stops
  // re-installing itself.
  ASSERT_EQ(c.getCount(), 0);
}

// Test that EventBase::loop() works as expected without time measurements.
TYPED_TEST_P(EventBaseTest, RunInLoopNoTimeMeasurement) {
  auto evbPtr = getEventBase<TypeParam>(
      EventBase::Options().setSkipTimeMeasurement(true));
  SKIP_IF(!evbPtr) << "Backend not available";
  folly::EventBase& eventBase = *evbPtr;

  CountedLoopCallback c(&eventBase, 10);
  eventBase.runInLoop(&c);
  // The callback shouldn't have run immediately
  ASSERT_EQ(c.getCount(), 10);
  eventBase.loop();

  // loop() should loop until the CountedLoopCallback stops
  // re-installing itself.
  ASSERT_EQ(c.getCount(), 0);
}

// Test runInLoop() calls with terminateLoopSoon()
TYPED_TEST_P(EventBaseTest, RunInLoopStopLoop) {
  auto evbPtr = getEventBase<TypeParam>();
  SKIP_IF(!evbPtr) << "Backend not available";
  folly::EventBase& eventBase = *evbPtr;

  CountedLoopCallback c1(&eventBase, 20);
  CountedLoopCallback c2(
      &eventBase, 10, std::bind(&EventBase::terminateLoopSoon, &eventBase));

  eventBase.runInLoop(&c1);
  eventBase.runInLoop(&c2);
  ASSERT_EQ(c1.getCount(), 20);
  ASSERT_EQ(c2.getCount(), 10);

  eventBase.loopForever();

  // c2 should have stopped the loop after 10 iterations
  ASSERT_EQ(c2.getCount(), 0);

  // We allow the EventBase to run the loop callbacks in whatever order it
  // chooses.  We'll accept c1's count being either 10 (if the loop terminated
  // after c1 ran on the 10th iteration) or 11 (if c2 terminated the loop
  // before c1 ran).
  //
  // (With the current code, c1 will always run 10 times, but we don't consider
  // this a hard API requirement.)
  ASSERT_GE(c1.getCount(), 10);
  ASSERT_LE(c1.getCount(), 11);
}

// Test loopPoll() call sequence
TYPED_TEST_P(EventBaseTest, RunPollLoop) {
  auto evbPtr = getEventBase<TypeParam>();
  SKIP_IF(!evbPtr) << "Backend not available";
  folly::EventBase& eventBase = *evbPtr;
  std::atomic<bool> running = true;
  int calls = 0;

  CountedLoopCallback c1(&eventBase, 20);
  CountedLoopCallback c2(
      &eventBase, 10, [eb = &eventBase, running = &running]() {
        eb->terminateLoopSoon();
        running->store(false);
      });

  eventBase.runInLoop(&c1);
  eventBase.runInLoop(&c2);
  ASSERT_EQ(c1.getCount(), 20);
  ASSERT_EQ(c2.getCount(), 10);

  eventBase.loopPollSetup();
  while (running.load()) {
    calls++;
    eventBase.loopPoll();
  }
  eventBase.loopPollCleanup();

  // We expect multiple iterations of the loop to happen, since loopPoll has non
  // blocking semantics, we should call loopPoll multiple times
  ASSERT_GT(calls, 1);
}

TYPED_TEST_P(EventBaseTest1, pidCheck) {
  auto evbPtr = getEventBase<TypeParam>();
  SKIP_IF(!evbPtr) << "Backend not available";

  auto deadManWalking = [&]() { evbPtr->loopForever(); };
  EXPECT_DEATH(deadManWalking(), "pid");
}

TYPED_TEST_P(EventBaseTest, MessageAvailableException) {
  auto evbPtr = getEventBase<TypeParam>();
  SKIP_IF(!evbPtr) << "Backend not available";

  auto deadManWalking = []() {
    auto evb = getEventBase<TypeParam>();
    std::thread t([&] {
      // Call this from another thread to force use of NotificationQueue in
      // runInEventBaseThread
      evb->runInEventBaseThread([]() { throw std::runtime_error("boom"); });
    });
    t.join();
    evb->loopForever();
  };
  EXPECT_DEATH(deadManWalking(), "boom");
}

TYPED_TEST_P(EventBaseTest, TryRunningAfterTerminate) {
  auto eventBasePtr = getEventBase<TypeParam>();
  SKIP_IF(!eventBasePtr) << "Backend not available";
  folly::EventBase& eventBase = *eventBasePtr;

  bool ran = false;
  CountedLoopCallback c1(
      &eventBase, 1, std::bind(&EventBase::terminateLoopSoon, &eventBase));
  eventBase.runInLoop(&c1);
  eventBase.loopForever();
  eventBase.runInEventBaseThread([&]() { ran = true; });

  ASSERT_FALSE(ran);

  eventBasePtr.reset();
  // Loop callbacks are triggered on EventBase destruction
  ASSERT_TRUE(ran);
}

// Test cancelling runInLoop() callbacks
TYPED_TEST_P(EventBaseTest, CancelRunInLoop) {
  auto eventBasePtr = getEventBase<TypeParam>();
  SKIP_IF(!eventBasePtr) << "Backend not available";
  folly::EventBase& eventBase = *eventBasePtr;

  CountedLoopCallback c1(&eventBase, 20);
  CountedLoopCallback c2(&eventBase, 20);
  CountedLoopCallback c3(&eventBase, 20);

  std::function<void()> cancelC1Action =
      std::bind(&EventBase::LoopCallback::cancelLoopCallback, &c1);
  std::function<void()> cancelC2Action =
      std::bind(&EventBase::LoopCallback::cancelLoopCallback, &c2);

  CountedLoopCallback cancelC1(&eventBase, 10, cancelC1Action);
  CountedLoopCallback cancelC2(&eventBase, 10, cancelC2Action);

  // Install cancelC1 after c1
  eventBase.runInLoop(&c1);
  eventBase.runInLoop(&cancelC1);

  // Install cancelC2 before c2
  eventBase.runInLoop(&cancelC2);
  eventBase.runInLoop(&c2);

  // Install c3
  eventBase.runInLoop(&c3);

  ASSERT_EQ(c1.getCount(), 20);
  ASSERT_EQ(c2.getCount(), 20);
  ASSERT_EQ(c3.getCount(), 20);
  ASSERT_EQ(cancelC1.getCount(), 10);
  ASSERT_EQ(cancelC2.getCount(), 10);

  // Run the loop
  eventBase.loop();

  // cancelC1 and cancelC2 should have both fired after 10 iterations and
  // stopped re-installing themselves
  ASSERT_EQ(cancelC1.getCount(), 0);
  ASSERT_EQ(cancelC2.getCount(), 0);
  // c3 should have continued on for the full 20 iterations
  ASSERT_EQ(c3.getCount(), 0);

  // c1 and c2 should have both been cancelled on the 10th iteration.
  //
  // Callbacks are always run in the order they are installed,
  // so c1 should have fired 10 times, and been canceled after it ran on the
  // 10th iteration.  c2 should have only fired 9 times, because cancelC2 will
  // have run before it on the 10th iteration, and cancelled it before it
  // fired.
  ASSERT_EQ(c1.getCount(), 10);
  ASSERT_EQ(c2.getCount(), 11);
}

namespace {
class TerminateTestCallback : public EventBase::LoopCallback,
                              public EventHandler {
 public:
  TerminateTestCallback(EventBase* eventBase, int fd)
      : EventHandler(eventBase, NetworkSocket::fromFd(fd)),
        eventBase_(eventBase),
        loopInvocations_(0),
        maxLoopInvocations_(0),
        eventInvocations_(0),
        maxEventInvocations_(0) {}

  void reset(uint32_t maxLoopInvocations, uint32_t maxEventInvocations) {
    loopInvocations_ = 0;
    maxLoopInvocations_ = maxLoopInvocations;
    eventInvocations_ = 0;
    maxEventInvocations_ = maxEventInvocations;

    cancelLoopCallback();
    unregisterHandler();
  }

  void handlerReady(uint16_t /* events */) noexcept override {
    // We didn't register with PERSIST, so we will have been automatically
    // unregistered already.
    ASSERT_FALSE(isHandlerRegistered());

    ++eventInvocations_;
    if (eventInvocations_ >= maxEventInvocations_) {
      return;
    }

    eventBase_->runInLoop(this);
  }
  void runLoopCallback() noexcept override {
    ++loopInvocations_;
    if (loopInvocations_ >= maxLoopInvocations_) {
      return;
    }

    registerHandler(READ);
  }

  uint32_t getLoopInvocations() const { return loopInvocations_; }
  uint32_t getEventInvocations() const { return eventInvocations_; }

 private:
  EventBase* eventBase_;
  uint32_t loopInvocations_;
  uint32_t maxLoopInvocations_;
  uint32_t eventInvocations_;
  uint32_t maxEventInvocations_;
};
} // namespace

/**
 * Test that EventBase::loop() correctly detects when there are no more events
 * left to run.
 *
 * This uses a single callback, which alternates registering itself as a loop
 * callback versus a EventHandler callback.  This exercises a regression where
 * EventBase::loop() incorrectly exited if there were no more fd handlers
 * registered, but a loop callback installed a new fd handler.
 */
TYPED_TEST_P(EventBaseTest, LoopTermination) {
  auto eventBasePtr = getEventBase<TypeParam>();
  SKIP_IF(!eventBasePtr) << "Backend not available";
  folly::EventBase& eventBase = *eventBasePtr;

  // Open a pipe and close the write end,
  // so the read endpoint will be readable
  int pipeFds[2];
  int rc = pipe(pipeFds);
  ASSERT_EQ(rc, 0);
  close(pipeFds[1]);
  TerminateTestCallback callback(&eventBase, pipeFds[0]);

  // Test once where the callback will exit after a loop callback
  callback.reset(10, 100);
  eventBase.runInLoop(&callback);
  eventBase.loop();
  ASSERT_EQ(callback.getLoopInvocations(), 10);
  ASSERT_EQ(callback.getEventInvocations(), 9);

  // Test once where the callback will exit after an fd event callback
  callback.reset(100, 7);
  eventBase.runInLoop(&callback);
  eventBase.loop();
  ASSERT_EQ(callback.getLoopInvocations(), 7);
  ASSERT_EQ(callback.getEventInvocations(), 7);

  close(pipeFds[0]);
}

TYPED_TEST_P(EventBaseTest, CallbackOrderTest) {
  size_t num = 0;
  auto eventBasePtr = getEventBase<TypeParam>();
  SKIP_IF(!eventBasePtr) << "Backend not available";
  folly::EventBase& evb = *eventBasePtr;

  evb.runInEventBaseThread([&]() {
    std::thread t([&]() {
      evb.runInEventBaseThread([&]() {
        num++;
        EXPECT_EQ(num, 2);
      });
    });
    t.join();

    // this callback will run first
    // even if it is scheduled after the first one
    evb.runInEventBaseThread([&]() {
      num++;
      EXPECT_EQ(num, 1);
    });
  });

  evb.loop();
  EXPECT_EQ(num, 2);
}

TYPED_TEST_P(EventBaseTest, AlwaysEnqueueCallbackOrderTest) {
  size_t num = 0;
  auto eventBasePtr = getEventBase<TypeParam>();
  SKIP_IF(!eventBasePtr) << "Backend not available";
  folly::EventBase& evb = *eventBasePtr;

  evb.runInEventBaseThread([&]() {
    std::thread t([&]() {
      evb.runInEventBaseThreadAlwaysEnqueue([&]() {
        num++;
        EXPECT_EQ(num, 1);
      });
    });
    t.join();

    // this callback will run second
    // since it was enqueued after the first one
    evb.runInEventBaseThreadAlwaysEnqueue([&]() {
      num++;
      EXPECT_EQ(num, 2);
    });
  });

  evb.loop();
  EXPECT_EQ(num, 2);
}

TYPED_TEST_P(EventBaseTest1, InternalExternalCallbackOrderTest) {
  size_t counter = 0;

  auto eventBasePtr = getEventBase<TypeParam>();
  SKIP_IF(!eventBasePtr) << "Backend not available";
  folly::EventBase& evb = *eventBasePtr;

  std::vector<size_t> calls;

  folly::Function<void(size_t)> runInLoopRecursive = [&](size_t left) {
    evb.runInLoop([&, left]() mutable {
      calls.push_back(counter++);
      if (--left == 0) {
        evb.terminateLoopSoon();
        return;
      }
      runInLoopRecursive(left);
    });
  };

  evb.runInEventBaseThread([&] { runInLoopRecursive(5); });
  for (size_t i = 0; i < 49; ++i) {
    evb.runInEventBaseThread([&] { ++counter; });
  }
  evb.loopForever();

  EXPECT_EQ(std::vector<size_t>({9, 20, 31, 42, 53}), calls);
}

///////////////////////////////////////////////////////////////////////////
// Tests for latency calculations
///////////////////////////////////////////////////////////////////////////

namespace {
class IdleTimeTimeoutSeries : public AsyncTimeout {
 public:
  explicit IdleTimeTimeoutSeries(
      EventBase* base, std::deque<std::size_t>& timeout)
      : AsyncTimeout(base), timeouts_(0), timeout_(timeout) {
    scheduleTimeout(1);
  }

  ~IdleTimeTimeoutSeries() override {}

  void timeoutExpired() noexcept override {
    ++timeouts_;

    if (timeout_.empty()) {
      cancelTimeout();
    } else {
      std::size_t sleepTime = timeout_.front();
      timeout_.pop_front();
      if (sleepTime) {
        usleep(sleepTime);
      }
      scheduleTimeout(1);
    }
  }

  int getTimeouts() const { return timeouts_; }

 private:
  int timeouts_;
  std::deque<std::size_t>& timeout_;
};
} // namespace

/**
 * Verify that idle time is correctly accounted for when decaying our loop
 * time.
 *
 * This works by creating a high loop time (via usleep), expecting a latency
 * callback with known value, and then scheduling a timeout for later. This
 * later timeout is far enough in the future that the idle time should have
 * caused the loop time to decay.
 */
TYPED_TEST_P(EventBaseTest, IdleTime) {
  auto eventBasePtr = getEventBase<TypeParam>();
  SKIP_IF(!eventBasePtr) << "Backend not available";
  folly::EventBase& eventBase = *eventBasePtr;
  std::deque<std::size_t> timeouts0(4, 8080);
  timeouts0.push_front(8000);
  timeouts0.push_back(14000);
  IdleTimeTimeoutSeries tos0(&eventBase, timeouts0);
  std::deque<std::size_t> timeouts(20, 20);
  std::unique_ptr<IdleTimeTimeoutSeries> tos;
  bool hostOverloaded = false;

  // Loop once before starting the main test.  This will run NotificationQueue
  // callbacks that get automatically installed when the EventBase is first
  // created.  We want to make sure they don't interfere with the timing
  // operations below.
  eventBase.loopOnce(EVLOOP_NONBLOCK);
  eventBase.setLoadAvgMsec(std::chrono::milliseconds(1000));
  eventBase.resetLoadAvg(5900.0);

  int latencyCallbacks = 0;
  eventBase.setMaxLatency(std::chrono::microseconds(6000), [&]() {
    ++latencyCallbacks;
    if (latencyCallbacks != 1 || tos0.getTimeouts() < 6) {
      // This could only happen if the host this test is running
      // on is heavily loaded.
      hostOverloaded = true;
      return;
    }

    EXPECT_EQ(6, tos0.getTimeouts());
    EXPECT_GE(6100, eventBase.getAvgLoopTime() - 1200);
    EXPECT_LE(6100, eventBase.getAvgLoopTime() + 1200);
    tos = std::make_unique<IdleTimeTimeoutSeries>(&eventBase, timeouts);
  });

  // Kick things off with an "immediate" timeout
  tos0.scheduleTimeout(1);

  eventBase.loop();

  if (hostOverloaded) {
    SKIP() << "host too heavily loaded to execute test";
  }

  ASSERT_EQ(1, latencyCallbacks);
  ASSERT_EQ(7, tos0.getTimeouts());
  ASSERT_GE(5900, eventBase.getAvgLoopTime() - 1200);
  ASSERT_LE(5900, eventBase.getAvgLoopTime() + 1200);
  ASSERT_TRUE(!!tos);
  ASSERT_EQ(21, tos->getTimeouts());
}

TYPED_TEST_P(EventBaseTest, MaxLatencyUndamped) {
  auto eventBasePtr = getEventBase<TypeParam>();
  folly::EventBase& eb = *eventBasePtr;
  int maxDurationViolations = 0;
  eb.setMaxLatency(
      std::chrono::milliseconds{1}, [&]() { maxDurationViolations++; }, false);
  eb.runInLoop(
      [&]() {
        /* sleep override */ std::this_thread::sleep_for(
            std::chrono::microseconds{1001});
        eb.terminateLoopSoon();
      },
      true);
  eb.loop();
  ASSERT_EQ(maxDurationViolations, 1);
}

TYPED_TEST_P(EventBaseTest, UnsetMaxLatencyUndamped) {
  auto eventBasePtr = getEventBase<TypeParam>();
  folly::EventBase& eb = *eventBasePtr;
  int maxDurationViolations = 0;
  eb.setMaxLatency(
      std::chrono::milliseconds{1}, [&]() { maxDurationViolations++; }, false);
  // Immediately unset it and make sure the counter isn't incremented. If the
  // function gets called, this will raise an std::bad_function_call.
  std::function<void()> bad_func = nullptr;
  eb.setMaxLatency(std::chrono::milliseconds{0}, bad_func, false);
  eb.runInLoop(
      [&]() {
        /* sleep override */ std::this_thread::sleep_for(
            std::chrono::microseconds{1001});
        eb.terminateLoopSoon();
      },
      true);
  eb.loop();
  ASSERT_EQ(maxDurationViolations, 0);
}

/**
 * Test that thisLoop functionality works with terminateLoopSoon
 */
TYPED_TEST_P(EventBaseTest, ThisLoop) {
  bool runInLoop = false;
  bool runThisLoop = false;

  {
    auto eventBasePtr = getEventBase<TypeParam>();
    SKIP_IF(!eventBasePtr) << "Backend not available";
    folly::EventBase& eb = *eventBasePtr;
    eb.runInLoop(
        [&]() {
          eb.terminateLoopSoon();
          eb.runInLoop([&]() { runInLoop = true; });
          eb.runInLoop([&]() { runThisLoop = true; }, true);
        },
        true);
    eb.loopForever();

    // Should not work
    ASSERT_FALSE(runInLoop);
    // Should work with thisLoop
    ASSERT_TRUE(runThisLoop);
  }

  // Pending loop callbacks will be run when the EventBase is destroyed.
  ASSERT_TRUE(runInLoop);
}

TYPED_TEST_P(EventBaseTest, EventBaseThreadLoop) {
  auto eventBasePtr = getEventBase<TypeParam>();
  SKIP_IF(!eventBasePtr) << "Backend not available";
  folly::EventBase& base = *eventBasePtr;
  bool ran = false;
  base.runInEventBaseThread([&]() { ran = true; });
  base.loop();

  ASSERT_TRUE(ran);
}

TYPED_TEST_P(EventBaseTest, EventBaseThreadName) {
  auto eventBasePtr = getEventBase<TypeParam>();
  SKIP_IF(!eventBasePtr) << "Backend not available";
  folly::EventBase& base = *eventBasePtr;
  base.setName("foo");
  base.loop();

  ASSERT_EQ("foo", *getCurrentThreadName());
}

TYPED_TEST_P(EventBaseTest, RunBeforeLoop) {
  auto eventBasePtr = getEventBase<TypeParam>();
  SKIP_IF(!eventBasePtr) << "Backend not available";
  folly::EventBase& base = *eventBasePtr;
  CountedLoopCallback cb(&base, 1, [&]() { base.terminateLoopSoon(); });
  base.runBeforeLoop(&cb);
  base.loopForever();
  ASSERT_EQ(cb.getCount(), 0);
}

TYPED_TEST_P(EventBaseTest, RunBeforeLoopWait) {
  auto eventBasePtr = getEventBase<TypeParam>();
  SKIP_IF(!eventBasePtr) << "Backend not available";
  folly::EventBase& base = *eventBasePtr;
  CountedLoopCallback cb(&base, 1);
  base.tryRunAfterDelay([&]() { base.terminateLoopSoon(); }, 500);
  base.runBeforeLoop(&cb);
  base.loopForever();

  // Check that we only ran once, and did not loop multiple times.
  ASSERT_EQ(cb.getCount(), 0);
}

namespace {
class PipeHandler : public EventHandler {
 public:
  PipeHandler(EventBase* eventBase, int fd)
      : EventHandler(eventBase, NetworkSocket::fromFd(fd)) {}

  void handlerReady(uint16_t /* events */) noexcept override { abort(); }
};
} // namespace

TYPED_TEST_P(EventBaseTest, StopBeforeLoop) {
  auto eventBasePtr = getEventBase<TypeParam>();
  SKIP_IF(!eventBasePtr) << "Backend not available";
  folly::EventBase& evb = *eventBasePtr;

  // Give the evb something to do.
  int p[2];
  ASSERT_EQ(0, pipe(p));
  PipeHandler handler(&evb, p[0]);
  handler.registerHandler(EventHandler::READ);

  // It's definitely not running yet
  evb.terminateLoopSoon();

  // let it run, it should exit quickly.
  std::thread t([&] { evb.loop(); });
  t.join();

  handler.unregisterHandler();
  close(p[0]);
  close(p[1]);

  SUCCEED();
}

TYPED_TEST_P(EventBaseTest, RunCallbacksOnDestruction) {
  bool ran = false;

  {
    auto eventBasePtr = getEventBase<TypeParam>();
    SKIP_IF(!eventBasePtr) << "Backend not available";
    eventBasePtr->runInEventBaseThread([&]() { ran = true; });
  }

  ASSERT_TRUE(ran);
}

TYPED_TEST_P(EventBaseTest, RunCallbacksPreDestruction) {
  bool ranPreDestruction = false;
  bool ranOnDestruction = false;
  auto evbPtr = getEventBase<TypeParam>();
  // Prevents the EventBase destruction from completing, but the pre destruction
  // callbacks should still be called.
  auto loopKeepAlive = getKeepAliveToken(*evbPtr);
  evbPtr->runOnDestruction([&] { ranOnDestruction = true; });
  evbPtr->runOnDestructionStart([&] {
    ASSERT_FALSE(ranOnDestruction);
    ranPreDestruction = true;
    loopKeepAlive.reset();
  });
  evbPtr.reset();
  ASSERT_TRUE(ranPreDestruction);
  ASSERT_TRUE(ranOnDestruction);
}

TYPED_TEST_P(EventBaseTest, LoopKeepAlive) {
  auto evbPtr = getEventBase<TypeParam>();
  SKIP_IF(!evbPtr) << "Backend not available";

  bool done = false;
  std::thread t([&, loopKeepAlive = getKeepAliveToken(*evbPtr)]() mutable {
    /* sleep override */ std::this_thread::sleep_for(
        std::chrono::milliseconds(100));
    evbPtr->runInEventBaseThread(
        [&done, loopKeepAlive = std::move(loopKeepAlive)] { done = true; });
  });

  evbPtr->loop();

  ASSERT_TRUE(done);

  t.join();
}

TYPED_TEST_P(EventBaseTest, LoopKeepAliveInLoop) {
  auto evbPtr = getEventBase<TypeParam>();
  SKIP_IF(!evbPtr) << "Backend not available";

  bool done = false;
  std::thread t;

  evbPtr->runInEventBaseThread([&] {
    t = std::thread([&, loopKeepAlive = getKeepAliveToken(*evbPtr)]() mutable {
      /* sleep override */ std::this_thread::sleep_for(
          std::chrono::milliseconds(100));
      evbPtr->runInEventBaseThread(
          [&done, loopKeepAlive = std::move(loopKeepAlive)] { done = true; });
    });
  });

  evbPtr->loop();

  ASSERT_TRUE(done);

  t.join();
}

TYPED_TEST_P(EventBaseTest, LoopKeepAliveWithLoopForever) {
  auto evbPtr = getEventBase<TypeParam>();
  SKIP_IF(!evbPtr) << "Backend not available";

  bool done = false;

  std::thread evThread([&] {
    evbPtr->loopForever();
    evbPtr.reset();
    done = true;
  });

  {
    auto* ev = evbPtr.get();
    Executor::KeepAlive<EventBase> keepAlive;
    ev->runInEventBaseThreadAndWait(
        [&ev, &keepAlive] { keepAlive = getKeepAliveToken(ev); });
    ASSERT_FALSE(done) << "Loop finished before we asked it to";
    ev->terminateLoopSoon();
    /* sleep override */
    std::this_thread::sleep_for(std::chrono::milliseconds(30));
    ASSERT_FALSE(done) << "Loop terminated early";
    ev->runInEventBaseThread([keepAlive = std::move(keepAlive)] {});
  }

  evThread.join();
  ASSERT_TRUE(done);
}

TYPED_TEST_P(EventBaseTest, LoopKeepAliveShutdown) {
  auto evbPtr = getEventBase<TypeParam>();
  SKIP_IF(!evbPtr) << "Backend not available";

  bool done = false;

  std::thread t([&done,
                 loopKeepAlive = getKeepAliveToken(evbPtr.get()),
                 evbPtrRaw = evbPtr.get()]() mutable {
    /* sleep override */ std::this_thread::sleep_for(
        std::chrono::milliseconds(100));
    evbPtrRaw->runInEventBaseThread(
        [&done, loopKeepAlive = std::move(loopKeepAlive)] { done = true; });
  });

  evbPtr.reset();

  ASSERT_TRUE(done);

  t.join();
}

TYPED_TEST_P(EventBaseTest, LoopKeepAliveAtomic) {
  auto evbPtr = getEventBase<TypeParam>();
  SKIP_IF(!evbPtr) << "Backend not available";

  static constexpr size_t kNumThreads = 100;
  static constexpr size_t kNumTasks = 100;

  std::vector<std::thread> ts;
  std::vector<std::unique_ptr<Baton<>>> batons;
  size_t done{0};

  for (size_t i = 0; i < kNumThreads; ++i) {
    batons.emplace_back(std::make_unique<Baton<>>());
  }

  for (size_t i = 0; i < kNumThreads; ++i) {
    ts.emplace_back([evbPtrRaw = evbPtr.get(),
                     batonPtr = batons[i].get(),
                     &done] {
      std::vector<Executor::KeepAlive<EventBase>> keepAlives;
      for (size_t j = 0; j < kNumTasks; ++j) {
        keepAlives.emplace_back(getKeepAliveToken(evbPtrRaw));
      }

      batonPtr->post();

      /* sleep override */ std::this_thread::sleep_for(std::chrono::seconds(1));

      for (auto& keepAlive : keepAlives) {
        evbPtrRaw->runInEventBaseThread(
            [&done, keepAlive = std::move(keepAlive)]() { ++done; });
      }
    });
  }

  for (auto& baton : batons) {
    baton->wait();
  }

  evbPtr.reset();

  EXPECT_EQ(kNumThreads * kNumTasks, done);

  for (auto& t : ts) {
    t.join();
  }
}

TYPED_TEST_P(EventBaseTest, LoopKeepAliveCast) {
  auto evbPtr = getEventBase<TypeParam>();
  SKIP_IF(!evbPtr) << "Backend not available";
  Executor::KeepAlive<> keepAlive = getKeepAliveToken(*evbPtr);
}

TYPED_TEST_P(EventBaseTest1, DrivableExecutorTest) {
  folly::Promise<bool> p;
  auto f = p.getFuture();
  auto eventBasePtr = getEventBase<TypeParam>();
  SKIP_IF(!eventBasePtr) << "Backend not available";
  folly::EventBase& base = *eventBasePtr;
  bool finished = false;

  Baton baton;

  std::thread t([&] {
    baton.wait();
    /* sleep override */
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    finished = true;
    base.runInEventBaseThread([&]() { p.setValue(true); });
  });

  // Ensure drive does not busy wait
  base.drive(); // TODO: fix notification queue init() extra wakeup
  baton.post();
  base.drive();
  EXPECT_TRUE(finished);

  folly::Promise<bool> p2;
  auto f2 = p2.getFuture();
  // Ensure waitVia gets woken up properly, even from
  // a separate thread.
  base.runAfterDelay([&]() { p2.setValue(true); }, 10);
  f2.waitVia(&base);
  EXPECT_TRUE(f2.isReady());

  t.join();
}

TYPED_TEST_P(EventBaseTest1, IOExecutorTest) {
  auto evbPtr = getEventBase<TypeParam>();
  SKIP_IF(!evbPtr) << "Backend not available";

  // Ensure EventBase manages itself as an IOExecutor.
  EXPECT_EQ(evbPtr->getEventBase(), evbPtr.get());
}

TYPED_TEST_P(EventBaseTest1, RequestContextTest) {
  auto evbPtr = getEventBase<TypeParam>();
  SKIP_IF(!evbPtr) << "Backend not available";
  auto defaultCtx = RequestContext::get();
  std::weak_ptr<RequestContext> rctx_weak_ptr;

  {
    RequestContextScopeGuard rctx;
    rctx_weak_ptr = RequestContext::saveContext();
    auto context = RequestContext::get();
    EXPECT_NE(defaultCtx, context);
    evbPtr->runInLoop([context] { EXPECT_EQ(context, RequestContext::get()); });
    evbPtr->loop();
  }

  // Ensure that RequestContext created for the scope has been released and
  // deleted.
  EXPECT_EQ(rctx_weak_ptr.expired(), true);

  EXPECT_EQ(defaultCtx, RequestContext::get());
}

TYPED_TEST_P(EventBaseTest1, CancelLoopCallbackRequestContextTest) {
  auto evbPtr = getEventBase<TypeParam>();
  SKIP_IF(!evbPtr) << "Backend not available";
  CountedLoopCallback c(evbPtr.get(), 1);

  auto defaultCtx = RequestContext::get();
  EXPECT_EQ(defaultCtx, RequestContext::get());
  std::weak_ptr<RequestContext> rctx_weak_ptr;

  {
    RequestContextScopeGuard rctx;
    rctx_weak_ptr = RequestContext::saveContext();
    auto context = RequestContext::get();
    EXPECT_NE(defaultCtx, context);
    evbPtr->runInLoop(&c);
    c.cancelLoopCallback();
  }

  // Ensure that RequestContext created for the scope has been released and
  // deleted.
  EXPECT_EQ(rctx_weak_ptr.expired(), true);

  EXPECT_EQ(defaultCtx, RequestContext::get());
}

TYPED_TEST_P(EventBaseTest1, TestStarvation) {
  auto evbPtr = getEventBase<TypeParam>();
  SKIP_IF(!evbPtr) << "Backend not available";

  Baton<> stopRequested;
  Baton<> stopScheduled;
  bool stopping{false};
  std::thread t{[&] {
    stopRequested.wait();
    evbPtr->add([&]() { stopping = true; });
    stopScheduled.post();
  }};

  size_t num{0};
  std::function<void()> fn;
  fn = [&]() {
    if (stopping || num >= 2000) {
      return;
    }

    if (++num == 1000) {
      stopRequested.post();
      stopScheduled.wait();
    }

    evbPtr->add(fn);
  };

  evbPtr->add(fn);
  evbPtr->loop();

  EXPECT_EQ(1000, num);
  t.join();
}

TYPED_TEST_P(EventBaseTest1, RunOnDestructionBasic) {
  bool ranOnDestruction = false;
  {
    auto evbPtr = getEventBase<TypeParam>();
    SKIP_IF(!evbPtr) << "Backend not available";
    evbPtr->runOnDestruction([&ranOnDestruction] { ranOnDestruction = true; });
  }
  EXPECT_TRUE(ranOnDestruction);
}

TYPED_TEST_P(EventBaseTest1, RunOnDestructionCancelled) {
  struct Callback : EventBase::OnDestructionCallback {
    bool ranOnDestruction{false};

    void onEventBaseDestruction() noexcept final { ranOnDestruction = true; }
  };

  auto cb = std::make_unique<Callback>();
  {
    auto evbPtr = getEventBase<TypeParam>();
    SKIP_IF(!evbPtr) << "Backend not available";
    evbPtr->runOnDestruction(*cb);
    EXPECT_TRUE(cb->cancel());
  }
  EXPECT_FALSE(cb->ranOnDestruction);
  EXPECT_FALSE(cb->cancel());
}

TYPED_TEST_P(EventBaseTest1, RunOnDestructionAfterHandleDestroyed) {
  auto evbPtr = getEventBase<TypeParam>();
  SKIP_IF(!evbPtr) << "Backend not available";
  {
    bool ranOnDestruction = false;
    auto* cb = new EventBase::FunctionOnDestructionCallback(
        [&ranOnDestruction] { ranOnDestruction = true; });
    evbPtr->runOnDestruction(*cb);
    EXPECT_TRUE(cb->cancel());
    delete cb;
  }
}

TYPED_TEST_P(EventBaseTest1, RunOnDestructionAddCallbackWithinCallback) {
  size_t callbacksCalled = 0;
  {
    auto evbPtr = getEventBase<TypeParam>();
    SKIP_IF(!evbPtr) << "Backend not available";
    evbPtr->runOnDestruction([&] {
      ++callbacksCalled;
      evbPtr->runOnDestruction([&] { ++callbacksCalled; });
    });
  }
  EXPECT_EQ(2, callbacksCalled);
}

TYPED_TEST_P(EventBaseTest1, EventBaseExecutionObserver) {
  auto eventBasePtr = getEventBase<TypeParam>();
  SKIP_IF(!eventBasePtr) << "Backend not available";
  folly::EventBase& base = *eventBasePtr;
  bool ranBeforeLoop = false;
  bool ran = false;
  TestObserver observer;
  base.addExecutionObserver(&observer);

  CountedLoopCallback cb(&base, 1, [&]() { ranBeforeLoop = true; });
  base.runBeforeLoop(&cb);

  base.runInEventBaseThread(
      [&]() { base.runInEventBaseThread([&]() { ran = true; }); });
  base.loop();

  ASSERT_TRUE(ranBeforeLoop);
  ASSERT_TRUE(ran);
  ASSERT_EQ(0, observer.nestedStart_);
  ASSERT_EQ(4, observer.numStartingCalled_);
  ASSERT_EQ(4, observer.numStoppedCalled_);
}

TYPED_TEST_P(EventBaseTest, EventBaseObserver) {
  auto evbPtr = getEventBase<TypeParam>();
  auto observer1 = std::make_shared<TestEventBaseObserver>(2);
  evbPtr->setObserver(observer1);
  evbPtr->loopOnce();
  evbPtr->loopOnce();
  ASSERT_EQ(1, observer1->getNumTimesCalled());
  evbPtr->loopOnce();
  evbPtr->loopOnce();
  evbPtr->loopOnce();
  auto observer2 = std::make_shared<TestEventBaseObserver>(1);
  evbPtr->setObserver(observer2);
  evbPtr->loopOnce();
  ASSERT_EQ(1, observer2->getNumTimesCalled());
}

TYPED_TEST_P(EventBaseTest, LoopRearmsNotificationQueue) {
  auto evbPtr = getEventBase<TypeParam>();
  std::atomic<size_t> n = 0;
  evbPtr->runInEventBaseThread([&]() { n = 1; });
  evbPtr->loopOnce();
  EXPECT_EQ(n.load(), 1);
  // The notification queue is rearmed through a loop callback, ensure that the
  // loop executes it.
  EXPECT_EQ(evbPtr->getNumLoopCallbacks(), 0);
}

REGISTER_TYPED_TEST_SUITE_P(
    EventBaseTest,
    EventBaseThread,
    ReadEvent,
    ReadPersist,
    ReadImmediate,
    WriteEvent,
    WritePersist,
    WriteImmediate,
    ReadWrite,
    WriteRead,
    ReadWriteSimultaneous,
    ReadWritePersist,
    ReadPartial,
    WritePartial,
    DestroyingHandler,
    RunAfterDelay,
    RunAfterDelayDestruction,
    BasicTimeouts,
    ReuseTimeout,
    RescheduleTimeout,
    CancelTimeout,
    DestroyingTimeout,
    ScheduledFn,
    ScheduledFnAt,
    RunInThread,
    RunInEventBaseThreadAndWait,
    RunImmediatelyOrRunInEventBaseThreadAndWaitCross,
    RunImmediatelyOrRunInEventBaseThreadAndWaitWithin,
    RunImmediatelyOrRunInEventBaseThreadAndWaitNotLooping,
    RunImmediatelyOrRunInEventBaseThreadCross,
    RunImmediatelyOrRunInEventBaseThreadNotLooping,
    RepeatedRunInLoop,
    RunInLoopNoTimeMeasurement,
    RunInLoopStopLoop,
    RunPollLoop,
    MessageAvailableException,
    TryRunningAfterTerminate,
    CancelRunInLoop,
    LoopTermination,
    CallbackOrderTest,
    AlwaysEnqueueCallbackOrderTest,
    IdleTime,
    MaxLatencyUndamped,
    UnsetMaxLatencyUndamped,
    ThisLoop,
    EventBaseThreadLoop,
    EventBaseThreadName,
    RunBeforeLoop,
    RunBeforeLoopWait,
    StopBeforeLoop,
    RunCallbacksPreDestruction,
    RunCallbacksOnDestruction,
    LoopKeepAlive,
    LoopKeepAliveInLoop,
    LoopKeepAliveWithLoopForever,
    LoopKeepAliveShutdown,
    LoopKeepAliveAtomic,
    LoopKeepAliveCast,
    EventBaseObserver,
    LoopRearmsNotificationQueue);

REGISTER_TYPED_TEST_SUITE_P(
    EventBaseTest1,
    DrivableExecutorTest,
    IOExecutorTest,
    RequestContextTest,
    CancelLoopCallbackRequestContextTest,
    TestStarvation,
    RunOnDestructionBasic,
    RunOnDestructionCancelled,
    RunOnDestructionAfterHandleDestroyed,
    RunOnDestructionAddCallbackWithinCallback,
    InternalExternalCallbackOrderTest,
    pidCheck,
    EventBaseExecutionObserver);

} // namespace test
} // namespace folly
